#include "FFmpegVideoReceiver.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtGui/QImageWriter>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QVideoSink>
#include <QtNetwork/QHostAddress>
#include <QtQuick/QQuickItem>
#include <atomic>
#include <climits>
#include <cstring>
#include <deque>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

struct FFmpegVideoReceiver::StreamState : std::enable_shared_from_this<StreamState>
{
    struct RecordingCommand {
        bool start;
        QString filename;
        FILE_FORMAT format = FILE_FORMAT_MP4;
    };

    FFmpegVideoReceiver *receiver = nullptr;
    QString uri;
    qint64 timeoutUs = 3000000;
    bool lowLatency = false;
    std::atomic_bool stopped = false;
    std::atomic<qint64> deadline = 0;
    STATUS result = STATUS_FAIL;
    int errorCode = 0;
    QMutex frameMutex;
    QVideoFrame frame;
    bool framePending = false;
    QMutex recordingMutex;
    std::deque<RecordingCommand> recordingCommands;
    AVFormatContext *input = nullptr;
    AVFormatContext *output = nullptr;
    AVCodecContext *decoder = nullptr;
    AVFrame *decoded = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *scaler = nullptr;
    AVIOContext *sdpIo = nullptr;
    QByteArray sdp;
    qsizetype sdpOffset = 0;
    int videoStream = -1;
    QString recordingFile;
    bool recordingHeader = false;
    int64_t recordingBase = AV_NOPTS_VALUE;
    int64_t recordingDts = AV_NOPTS_VALUE;
    int64_t firstFilePts = AV_NOPTS_VALUE;
    int64_t fileClock = 0;

    ~StreamState()
    {
        finishRecording(false);
        sws_freeContext(scaler);
        av_packet_free(&packet);
        av_frame_free(&decoded);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
        if (sdpIo) {
            av_freep(&sdpIo->buffer);
            avio_context_free(&sdpIo);
        }
    }

    static int interrupt(void *opaque)
    {
        const auto *state = static_cast<StreamState*>(opaque);
        return state->stopped || av_gettime_relative() > state->deadline;
    }

    static int readSdp(void *opaque, uint8_t *buffer, int size)
    {
        auto *state = static_cast<StreamState*>(opaque);
        const int count = int(qMin<qsizetype>(size, state->sdp.size() - state->sdpOffset));
        if (count <= 0) {
            return AVERROR_EOF;
        }
        memcpy(buffer, state->sdp.constData() + state->sdpOffset, count);
        state->sdpOffset += count;
        return count;
    }

    bool openInput()
    {
        input = avformat_alloc_context();
        if (!input) {
            return false;
        }
        input->interrupt_callback = { interrupt, this };
        input->flags |= AVFMT_FLAG_GENPTS;
        AVDictionary *options = nullptr;
        av_dict_set(&options, "protocol_whitelist", "file,http,tcp,udp,rtp,data", 0);
        av_dict_set(&options, "reorder_queue_size", lowLatency ? "0" : "256", 0);
        av_dict_set(&options, "analyzeduration", "1000000", 0);
        av_dict_set(&options, "probesize", "262144", 0);
        const QByteArray timeout = QByteArray::number(timeoutUs);
        av_dict_set(&options, "rw_timeout", timeout.constData(), 0);
        if (lowLatency) {
            av_dict_set(&options, "fflags", "nobuffer", 0);
        }
        const QUrl url(uri);
        const QString scheme = url.scheme().toLower();
        const AVInputFormat *format = nullptr;
        QByteArray source = uri.toUtf8();
        if (scheme == QStringLiteral("udp") || scheme == QStringLiteral("udp265")) {
            const bool ipv6 = QHostAddress(url.host()).protocol() == QAbstractSocket::IPv6Protocol;
            const QString family = ipv6 ? QStringLiteral("IP6") : QStringLiteral("IP4");
            const QString codec = scheme == QStringLiteral("udp265") ? QStringLiteral("H265") : QStringLiteral("H264");
            sdp = QStringLiteral("v=0\r\no=- 0 0 IN %1 %2\r\ns=QGC\r\nc=IN %1 %2\r\nt=0 0\r\nm=video %3 RTP/AVP 96\r\na=rtpmap:96 %4/90000\r\n")
                    .arg(family, url.host()).arg(url.port()).arg(codec).toUtf8();
            if (scheme == QStringLiteral("udp")) {
                sdp += "a=fmtp:96 packetization-mode=1\r\n";
            }
            uint8_t *buffer = static_cast<uint8_t*>(av_malloc(4096));
            if (!buffer) {
                av_dict_free(&options);
                return false;
            }
            sdpIo = avio_alloc_context(buffer, 4096, 0, this, readSdp, nullptr, nullptr);
            if (!sdpIo) {
                av_free(buffer);
                av_dict_free(&options);
                return false;
            }
            input->pb = sdpIo;
            input->flags |= AVFMT_FLAG_CUSTOM_IO;
            format = av_find_input_format("sdp");
            source.clear();
        } else if (scheme == QStringLiteral("mpegts")) {
            source = uri.toUtf8();
            source.replace("mpegts://", "udp://");
            format = av_find_input_format("mpegts");
        } else if (scheme == QStringLiteral("tcp")) {
            format = av_find_input_format("mpegts");
        } else if (scheme == QStringLiteral("file")) {
            source = QFile::encodeName(url.toLocalFile());
        }
        deadline = av_gettime_relative() + timeoutUs;
        const int opened = avformat_open_input(&input, source.isEmpty() ? nullptr : source.constData(), format, &options);
        av_dict_free(&options);
        errorCode = opened;
        if (opened < 0) {
            return false;
        }
        errorCode = avformat_find_stream_info(input, nullptr);
        if (errorCode < 0) {
            return false;
        }
        const AVCodec *codec = nullptr;
        videoStream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (videoStream < 0 || !codec) {
            errorCode = videoStream;
            return false;
        }
        decoder = avcodec_alloc_context3(codec);
        if (!decoder || avcodec_parameters_to_context(decoder, input->streams[videoStream]->codecpar) < 0) {
            return false;
        }
        decoder->thread_count = 2;
        if (lowLatency) {
            decoder->thread_type = FF_THREAD_SLICE;
        }
        errorCode = avcodec_open2(decoder, codec, nullptr);
        if (errorCode < 0) {
            return false;
        }
        packet = av_packet_alloc();
        decoded = av_frame_alloc();
        return packet && decoded;
    }

    void postRecording(bool start, STATUS status, const QString &filename = QString())
    {
        const auto state = shared_from_this();
        QMetaObject::invokeMethod(receiver, [state, start, status, filename]() {
            auto *owner = state->receiver;
            if (owner->_state != state) {
                return;
            }
            if (start) {
                owner->_recordStartPending = false;
                if (status == STATUS_OK) {
                    owner->_recordingOutput = filename;
                    owner->_setRecording(true);
                    emit owner->recordingStarted(filename);
                }
                emit owner->onStartRecordingComplete(status);
            } else {
                owner->_recordStopPending = false;
                owner->_setRecording(false);
                emit owner->onStopRecordingComplete(status);
            }
        }, Qt::QueuedConnection);
    }

    void finishRecording(bool notify, bool failed = false)
    {
        if (!output) {
            if (notify) {
                postRecording(false, STATUS_OK);
            }
            return;
        }
        const bool hadHeader = recordingHeader;
        const int status = recordingHeader ? av_write_trailer(output) : 0;
        const int closed = output->pb ? avio_closep(&output->pb) : 0;
        avformat_free_context(output);
        output = nullptr;
        recordingHeader = false;
        recordingBase = AV_NOPTS_VALUE;
        recordingDts = AV_NOPTS_VALUE;
        if (notify) {
            if (!hadHeader) {
                postRecording(true, STATUS_FAIL);
            }
            postRecording(false, !failed && status >= 0 && closed >= 0 ? STATUS_OK : STATUS_FAIL);
        }
    }

    void processRecordingCommands()
    {
        std::deque<RecordingCommand> commands;
        {
            QMutexLocker lock(&recordingMutex);
            commands.swap(recordingCommands);
        }
        for (const RecordingCommand &command : commands) {
            if (!command.start) {
                finishRecording(true);
                continue;
            }
            const char *format = command.format == FILE_FORMAT_MKV ? "matroska" : command.format == FILE_FORMAT_MOV ? "mov" : "mp4";
            const QByteArray filename = QFile::encodeName(command.filename);
            if (output || avformat_alloc_output_context2(&output, nullptr, format, filename.constData()) < 0 || !output) {
                postRecording(true, STATUS_FAIL);
                continue;
            }
            AVStream *stream = avformat_new_stream(output, nullptr);
            if (!stream || avcodec_parameters_copy(stream->codecpar, input->streams[videoStream]->codecpar) < 0) {
                finishRecording(false);
                postRecording(true, STATUS_FAIL);
                continue;
            }
            stream->codecpar->codec_tag = 0;
            stream->time_base = input->streams[videoStream]->time_base;
            if (avio_open(&output->pb, filename.constData(), AVIO_FLAG_WRITE) < 0) {
                finishRecording(false);
                postRecording(true, STATUS_FAIL);
                continue;
            }
            recordingFile = command.filename;
        }
    }

    void recordPacket()
    {
        if (!output) {
            return;
        }
        if (!recordingHeader) {
            if (!(packet->flags & AV_PKT_FLAG_KEY)) {
                return;
            }
            if (avformat_write_header(output, nullptr) < 0) {
                finishRecording(false);
                postRecording(true, STATUS_FAIL);
                return;
            }
            recordingHeader = true;
            recordingBase = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
            if (recordingBase == AV_NOPTS_VALUE) {
                recordingBase = 0;
            }
            postRecording(true, STATUS_OK, recordingFile);
        }
        AVPacket *copy = av_packet_clone(packet);
        if (!copy) {
            finishRecording(true, true);
            return;
        }
        if (copy->pts != AV_NOPTS_VALUE) {
            copy->pts -= recordingBase;
        }
        if (copy->dts != AV_NOPTS_VALUE) {
            copy->dts -= recordingBase;
        } else {
            copy->dts = copy->pts != AV_NOPTS_VALUE ? copy->pts : recordingDts == AV_NOPTS_VALUE ? 0 : recordingDts + qMax<int64_t>(1, copy->duration);
        }
        if (copy->pts == AV_NOPTS_VALUE) {
            copy->pts = copy->dts;
        }
        recordingDts = copy->dts;
        av_packet_rescale_ts(copy, input->streams[videoStream]->time_base, output->streams[0]->time_base);
        copy->stream_index = 0;
        copy->pos = -1;
        const int written = av_interleaved_write_frame(output, copy);
        av_packet_free(&copy);
        if (written < 0) {
            finishRecording(true, true);
        }
    }

    bool deliverFrame()
    {
        if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > 8192 || decoded->height > 8192) {
            return false;
        }
        if (QUrl(uri).isLocalFile() && decoded->best_effort_timestamp != AV_NOPTS_VALUE) {
            const int64_t pts = av_rescale_q(decoded->best_effort_timestamp, input->streams[videoStream]->time_base, AVRational{1, 1000000});
            if (firstFilePts == AV_NOPTS_VALUE) {
                firstFilePts = pts;
                fileClock = av_gettime_relative();
            }
            const int64_t target = fileClock + pts - firstFilePts;
            while (!stopped) {
                const int64_t remaining = target - av_gettime_relative();
                if (remaining <= 0) {
                    break;
                }
                av_usleep(unsigned(qMin<int64_t>(10000, remaining)));
            }
        }
        if (stopped) {
            return false;
        }
        const QSize size(decoded->width, decoded->height);
        QVideoFrameFormat format(size, QVideoFrameFormat::Format_YUV420P);
        format.setColorSpace(decoded->colorspace == AVCOL_SPC_BT709 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601);
        format.setColorRange(decoded->color_range == AVCOL_RANGE_JPEG ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);
        QVideoFrame outputFrame(format);
        if (!outputFrame.map(QVideoFrame::WriteOnly)) {
            return false;
        }
        scaler = sws_getCachedContext(scaler, decoded->width, decoded->height, AVPixelFormat(decoded->format),
                                     decoded->width, decoded->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        uint8_t *planes[4] = {outputFrame.bits(0), outputFrame.bits(1), outputFrame.bits(2), nullptr};
        int strides[4] = {outputFrame.bytesPerLine(0), outputFrame.bytesPerLine(1), outputFrame.bytesPerLine(2), 0};
        const int rows = scaler ? sws_scale(scaler, decoded->data, decoded->linesize, 0, decoded->height, planes, strides) : 0;
        outputFrame.unmap();
        if (rows != decoded->height) {
            return false;
        }
        deadline = av_gettime_relative() + timeoutUs;
        const auto state = shared_from_this();
        QMutexLocker lock(&frameMutex);
        frame = outputFrame;
        if (!framePending) {
            framePending = true;
            QMetaObject::invokeMethod(receiver, [state]() { state->receiver->_receiveFrame(state); }, Qt::QueuedConnection);
        }
        return true;
    }

    void run()
    {
        if (stopped || !openInput()) {
            return;
        }
        while (!stopped) {
            processRecordingCommands();
            const int read = av_read_frame(input, packet);
            if (read == AVERROR_EOF) {
                if (avcodec_send_packet(decoder, nullptr) >= 0) {
                    while (!stopped && avcodec_receive_frame(decoder, decoded) >= 0) {
                        deliverFrame();
                        av_frame_unref(decoded);
                    }
                }
                result = STATUS_OK;
                break;
            }
            if (read < 0) {
                errorCode = read;
                break;
            }
            if (packet->stream_index == videoStream) {
                recordPacket();
                const int sent = avcodec_send_packet(decoder, packet);
                if (sent >= 0) {
                    while (!stopped && avcodec_receive_frame(decoder, decoded) >= 0) {
                        deliverFrame();
                        av_frame_unref(decoded);
                    }
                }
            }
            av_packet_unref(packet);
        }
        if (output) {
            finishRecording(true);
        }
        if (stopped) {
            result = STATUS_OK;
        }
    }
};

FFmpegVideoReceiver::FFmpegVideoReceiver(QObject *parent)
    : VideoReceiver(parent)
{
    static std::once_flag initialized;
    std::call_once(initialized, []() { av_log_set_level(AV_LOG_QUIET); });
    connect(this, &VideoReceiver::uriChanged, this, [this]() {
        if (_state && _state->uri != _uri) {
            stop();
        }
    });
    _watchdogTimer.setSingleShot(true);
    connect(&_watchdogTimer, &QTimer::timeout, this, [this]() {
        if (_state) {
            _state->stopped = true;
            emit timeout();
        }
    });
}

FFmpegVideoReceiver::~FFmpegVideoReceiver()
{
    if (_state) {
        _state->stopped = true;
    }
    if (_thread) {
        _thread->wait();
    }
}

bool FFmpegVideoReceiver::enabled()
{
#ifdef QGC_FFMPEG_STREAMING
    return true;
#else
    return false;
#endif
}

void *FFmpegVideoReceiver::createVideoSink(QQuickItem *widget, QObject *)
{
    return widget ? qobject_cast<QVideoSink*>(widget->property("videoSink").value<QObject*>()) : nullptr;
}

void FFmpegVideoReceiver::releaseVideoSink(void *) {}
VideoReceiver *FFmpegVideoReceiver::createVideoReceiver(QObject *parent) { return new FFmpegVideoReceiver(parent); }

void FFmpegVideoReceiver::start(uint32_t timeoutSeconds)
{
    if (_state) {
        emit onStartComplete(STATUS_INVALID_STATE);
        return;
    }
    const QUrl url(_uri);
    const QString scheme = url.scheme().toLower();
    const QStringList schemes = {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("rtsp"), QStringLiteral("udp"),
                                 QStringLiteral("udp265"), QStringLiteral("mpegts"), QStringLiteral("tcp")};
    const bool needsPort = scheme == QStringLiteral("udp") || scheme == QStringLiteral("udp265") || scheme == QStringLiteral("mpegts") || scheme == QStringLiteral("tcp");
    if (!url.isValid() || !schemes.contains(scheme) || (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile()) ||
        (!url.isLocalFile() && url.host().isEmpty()) ||
        (needsPort && (url.host().isEmpty() || url.port() <= 0 || url.port() > 65535))) {
        emit onStartComplete(STATUS_INVALID_URL);
        return;
    }
    _timeout = qBound<uint32_t>(1, timeoutSeconds, uint32_t(INT_MAX / 1000));
    _lastFrame = QVideoFrame();
    _startPending = true;
    _stopPending = false;
    _state = std::make_shared<StreamState>();
    _state->receiver = this;
    _state->uri = _uri;
    _state->timeoutUs = qint64(_timeout) * 1000000;
    _state->lowLatency = _lowLatency;
    const auto state = _state;
    _thread = QThread::create([state]() { state->run(); });
    _thread->setParent(this);
    QThread *thread = _thread;
    connect(thread, &QThread::finished, this, [this, state, thread]() {
        _finishStream(state);
        thread->deleteLater();
    });
    _watchdogTimer.start(int(_timeout * 1000));
    _thread->start();
}

void FFmpegVideoReceiver::stop()
{
    if (!_state) {
        emit onStopComplete(STATUS_INVALID_STATE);
        return;
    }
    _stopPending = true;
    _state->stopped = true;
    _watchdogTimer.stop();
}

void FFmpegVideoReceiver::_receiveFrame(const std::shared_ptr<StreamState> &state)
{
    if (_state != state || state->stopped) {
        return;
    }
    {
        QMutexLocker lock(&state->frameMutex);
        _lastFrame = state->frame;
        state->framePending = false;
    }
    _watchdogTimer.start(int(_timeout * 1000));
    if (!_streaming) {
        _streaming = true;
        emit streamingChanged(true);
    }
    if (_startPending) {
        _startPending = false;
        setStarted(true);
        emit onStartComplete(STATUS_OK);
    }
    if (_lastFrame.size() != _videoSize) {
        _videoSize = _lastFrame.size();
        emit videoSizeChanged(_videoSize);
    }
    if (_videoSink) {
        _videoSink->setVideoFrame(_lastFrame);
        _setDecoding(true);
    }
}

void FFmpegVideoReceiver::_finishStream(const std::shared_ptr<StreamState> &state)
{
    if (_state != state) {
        return;
    }
    if (!state->stopped && state->result != STATUS_OK) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(state->errorCode, error, sizeof(error));
        qWarning() << "FFmpeg video source failed:" << error << state->errorCode;
    }
    _thread = nullptr;
    _state.reset();
    _watchdogTimer.stop();
    setStarted(false);
    if (_startPending) {
        _startPending = false;
        emit onStartComplete(STATUS_FAIL);
    }
    if (_recordStartPending) {
        _recordStartPending = false;
        emit onStartRecordingComplete(STATUS_FAIL);
    }
    if (_recordStopPending) {
        _recordStopPending = false;
        emit onStopRecordingComplete(STATUS_FAIL);
    }
    _setRecording(false);
    _setDecoding(false);
    if (_videoSink) {
        _videoSink->setVideoFrame(QVideoFrame());
    }
    _lastFrame = QVideoFrame();
    if (_streaming) {
        _streaming = false;
        emit streamingChanged(false);
    }
    const STATUS status = _stopPending ? STATUS_OK : state->result;
    _stopPending = false;
    emit onStopComplete(status);
}

void FFmpegVideoReceiver::_setDecoding(bool active)
{
    if (_decoding != active) {
        _decoding = active;
        emit decodingChanged(active);
    }
}

void FFmpegVideoReceiver::_setRecording(bool active)
{
    if (_recording != active) {
        _recording = active;
        emit recordingChanged(active);
    }
}

void FFmpegVideoReceiver::startDecoding(void *sink)
{
    if (!sink) {
        emit onStartDecodingComplete(STATUS_FAIL);
        return;
    }
    _videoSink = static_cast<QVideoSink*>(sink);
    if (_lastFrame.isValid()) {
        _videoSink->setVideoFrame(_lastFrame);
        _setDecoding(true);
    }
    emit onStartDecodingComplete(STATUS_OK);
}

void FFmpegVideoReceiver::stopDecoding()
{
    if (!_videoSink) {
        emit onStopDecodingComplete(STATUS_INVALID_STATE);
        return;
    }
    _videoSink->setVideoFrame(QVideoFrame());
    _videoSink.clear();
    _setDecoding(false);
    emit onStopDecodingComplete(STATUS_OK);
}

void FFmpegVideoReceiver::startRecording(const QString &videoFile, FILE_FORMAT format)
{
    if (!_state || !_streaming || _recording || _recordStartPending || _recordStopPending) {
        emit onStartRecordingComplete(STATUS_INVALID_STATE);
        return;
    }
    if (videoFile.isEmpty() || !isValidFileFormat(format) || QFileInfo::exists(videoFile)) {
        emit onStartRecordingComplete(STATUS_FAIL);
        return;
    }
    _recordStartPending = true;
    QMutexLocker lock(&_state->recordingMutex);
    _state->recordingCommands.push_back({true, videoFile, format});
}

void FFmpegVideoReceiver::stopRecording()
{
    if (!_state || (!_recording && !_recordStartPending) || _recordStopPending) {
        emit onStopRecordingComplete(STATUS_INVALID_STATE);
        return;
    }
    _recordStopPending = true;
    QMutexLocker lock(&_state->recordingMutex);
    _state->recordingCommands.push_back({false, QString()});
}

void FFmpegVideoReceiver::takeScreenshot(const QString &imageFile)
{
    const QImage image = _lastFrame.toImage();
    if (imageFile.isEmpty() || image.isNull()) {
        emit onTakeScreenshotComplete(STATUS_FAIL);
        return;
    }
    QSaveFile file(imageFile);
    QImageWriter writer(&file, QFileInfo(imageFile).suffix().toLatin1());
    const bool saved = file.open(QIODevice::WriteOnly) && writer.write(image) && file.commit();
    emit onTakeScreenshotComplete(saved ? STATUS_OK : STATUS_FAIL);
}
