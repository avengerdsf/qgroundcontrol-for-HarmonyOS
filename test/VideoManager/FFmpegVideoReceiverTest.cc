#include "FFmpegVideoReceiverTest.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtGui/QImage>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QVideoSink>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "FFmpegVideoReceiver.h"
#ifndef QGC_FFMPEG_NATIVE_TEST
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include "SettingsManager.h"
#include "VideoManager.h"
#include "VideoSettings.h"
#endif

extern "C" {
#include <libavformat/avformat.h>
}

namespace {
class RtpVideoFixture
{
public:
    bool load(const QString& path)
    {
        AVFormatContext* input = nullptr;
        const QByteArray filename = QFile::encodeName(path);
        if (avformat_open_input(&input, filename.constData(), nullptr, nullptr) < 0) {
            return false;
        }
        const int stream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream < 0) {
            avformat_close_input(&input);
            return false;
        }
        const AVCodecParameters* codec = input->streams[stream]->codecpar;
        const uint8_t* config = codec->extradata;
        const int size = codec->extradata_size;
        bool valid = config && size >= 7 && config[0] == 1;
        int offset = 6;
        const int lengthBytes = valid ? (config[4] & 3) + 1 : 0;
        int count = valid ? config[5] & 31 : 0;
        for (int group = 0; valid && group < 2; ++group) {
            for (int index = 0; valid && index < count; ++index) {
                if (offset + 2 > size) {
                    valid = false;
                    break;
                }
                const int length = qFromBigEndian<quint16>(config + offset);
                offset += 2;
                valid = offset + length <= size;
                if (valid) {
                    _configuration.append(QByteArray(reinterpret_cast<const char*>(config + offset), length));
                    offset += length;
                }
            }
            if (group == 0) {
                valid = valid && offset < size;
                count = valid ? config[offset++] : 0;
            }
        }
        AVPacket* packet = av_packet_alloc();
        while (valid && packet && av_read_frame(input, packet) >= 0) {
            if (packet->stream_index == stream) {
                QList<QByteArray> nals;
                int position = 0;
                while (position + lengthBytes <= packet->size) {
                    quint32 length = 0;
                    for (int byte = 0; byte < lengthBytes; ++byte) {
                        length = (length << 8) | packet->data[position++];
                    }
                    if (!length || length > quint32(packet->size - position)) {
                        valid = false;
                        break;
                    }
                    nals.append(QByteArray(reinterpret_cast<const char*>(packet->data + position), int(length)));
                    position += int(length);
                }
                _frames.append(nals);
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        avformat_close_input(&input);
        return valid && !_frames.isEmpty() && !_configuration.isEmpty();
    }

    quint16 serverPort()
    {
        if (!_socket.localPort() && !_socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0))) {
            return 0;
        }
        return _socket.localPort();
    }

    void start(quint16 port, QTcpSocket* interleaved = nullptr, quint8 channel = 0)
    {
        _frame = 0;
        _port = port;
        _interleaved = interleaved;
        _channel = channel;
        QObject::connect(&_timer, &QTimer::timeout, &_timer, [this]() {
            for (const QByteArray& nal : _configuration) {
                sendNal(nal, false);
            }
            const auto& frame = _frames.at(_frame++ % _frames.size());
            for (int index = 0; index < frame.size(); ++index) {
                sendNal(frame.at(index), index == frame.size() - 1);
            }
            _timestamp += 6000;
        });
        _timer.start(67);
    }

    void stop()
    {
        _timer.stop();
        _interleaved.clear();
        _port = 0;
    }

private:
    void sendPacket(const QByteArray& payload, bool marker)
    {
        QByteArray packet(12, '\0');
        packet[0] = char(0x80);
        packet[1] = char(96 | (marker ? 0x80 : 0));
        qToBigEndian<quint16>(_sequence++, packet.data() + 2);
        qToBigEndian<quint32>(_timestamp, packet.data() + 4);
        qToBigEndian<quint32>(0x51474301, packet.data() + 8);
        packet += payload;
        if (_interleaved) {
            QByteArray header(4, '\0');
            header[0] = '$';
            header[1] = char(_channel);
            qToBigEndian<quint16>(quint16(packet.size()), header.data() + 2);
            _interleaved->write(header + packet);
        } else if (_port) {
            _socket.writeDatagram(packet, QHostAddress::LocalHost, _port);
        }
    }

    void sendNal(const QByteArray& nal, bool marker)
    {
        if (nal.size() <= 1000) {
            sendPacket(nal, marker);
            return;
        }
        for (qsizetype offset = 1; offset < nal.size(); offset += 998) {
            const qsizetype count = qMin<qsizetype>(998, nal.size() - offset);
            const bool end = offset + count == nal.size();
            QByteArray payload;
            payload += char((quint8(nal[0]) & 0xe0) | 28);
            payload += char((quint8(nal[0]) & 0x1f) | (offset == 1 ? 0x80 : 0) | (end ? 0x40 : 0));
            payload += nal.mid(offset, count);
            sendPacket(payload, marker && end);
        }
    }

    QList<QByteArray> _configuration;
    QList<QList<QByteArray>> _frames;
    QUdpSocket _socket;
    QTimer _timer;
    QPointer<QTcpSocket> _interleaved;
    quint8 _channel = 0;
    quint16 _port = 0;
    quint16 _sequence = 0;
    quint32 _timestamp = 0;
    qsizetype _frame = 0;
};

class RtspVideoFixture
{
public:
    explicit RtspVideoFixture(bool tcpOnly) : _tcpOnly(tcpOnly)
    {
        QObject::connect(&_server, &QTcpServer::newConnection, &_server, [this]() {
            _video.stop();
            if (_client) {
                QObject::disconnect(_client, nullptr, &_server, nullptr);
                _client->deleteLater();
            }
            _requests.clear();
            _client = _server.nextPendingConnection();
            QObject::connect(_client, &QTcpSocket::readyRead, &_server, [this]() {
                _requests += _client->readAll();
                consumeRequests();
            });
        });
    }

    bool start(const QString& video)
    {
        return _video.load(video) && _video.serverPort() && _server.listen(QHostAddress::LocalHost, 0);
    }

    QString uri() const { return QStringLiteral("rtsp://127.0.0.1:%1/stream").arg(_server.serverPort()); }

    bool played() const { return _played; }

    QStringList transports() const { return _transports; }

    void disconnectClient()
    {
        _video.stop();
        if (_client) {
            _client->abort();
        }
    }

    void stop()
    {
        disconnectClient();
        _server.close();
    }

private:
    void reply(const QByteArray& sequence, int status, const QByteArray& headers = {}, const QByteArray& body = {})
    {
        const QByteArray reason = status == 200 ? "OK" : "Unsupported Transport";
        _client->write("RTSP/1.0 " + QByteArray::number(status) + ' ' + reason + "\r\nCSeq: " + sequence +
                       "\r\nSession: qgc-test\r\n" + headers + "Content-Length: " + QByteArray::number(body.size()) +
                       "\r\n\r\n" + body);
    }

    void consumeRequests()
    {
        while (!_requests.isEmpty()) {
            // RTCP receiver reports share the control socket in interleaved mode.
            if (_requests[0] == '$') {
                if (_requests.size() < 4) {
                    return;
                }
                const qsizetype size = 4 + qFromBigEndian<quint16>(_requests.constData() + 2);
                if (_requests.size() < size) {
                    return;
                }
                _requests.remove(0, size);
                continue;
            }
            const qsizetype end = _requests.indexOf("\r\n\r\n");
            if (end < 0) {
                return;
            }
            const QByteArray request = _requests.first(end);
            QByteArray sequence;
            QByteArray transport;
            qsizetype bodySize = 0;
            for (const QByteArray& line : request.split('\n')) {
                if (line.startsWith("CSeq:")) {
                    sequence = line.mid(5).trimmed();
                }
                if (line.startsWith("Transport:")) {
                    transport = line.mid(10).trimmed();
                }
                if (line.startsWith("Content-Length:")) {
                    bodySize = line.mid(15).trimmed().toLongLong();
                }
            }
            if (_requests.size() < end + 4 + bodySize) {
                return;
            }
            _requests.remove(0, end + 4 + bodySize);
            const QByteArray method = request.first(request.indexOf(' '));
            if (method == "OPTIONS") {
                reply(sequence, 200, "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n");
            } else if (method == "DESCRIBE") {
                const QByteArray sdp =
                    "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=QGC RTSP test\r\n"
                    "c=IN IP4 127.0.0.1\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\n"
                    "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=control:trackID=0\r\n";
                reply(sequence, 200, "Content-Type: application/sdp\r\nContent-Base: " + uri().toUtf8() + "/\r\n", sdp);
            } else if (method == "SETUP") {
                _transports.append(QString::fromLatin1(transport));
                const bool tcp = transport.contains("RTP/AVP/TCP");
                if (tcp != _tcpOnly) {
                    reply(sequence, 461);
                    continue;
                }
                const QRegularExpression field(tcp ? QStringLiteral("interleaved=(\\d+)-(\\d+)")
                                                   : QStringLiteral("client_port=(\\d+)-(\\d+)"));
                const auto match = field.match(QString::fromLatin1(transport));
                if (!match.hasMatch()) {
                    reply(sequence, 461);
                    continue;
                }
                _destination = match.captured(1).toUShort();
                if (tcp) {
                    reply(sequence, 200,
                          "Transport: RTP/AVP/TCP;unicast;interleaved=" + match.captured(1).toLatin1() + '-' +
                              match.captured(2).toLatin1() + "\r\n");
                } else {
                    const quint16 port = _video.serverPort();
                    reply(sequence, 200,
                          "Transport: RTP/AVP/UDP;unicast;client_port=" + match.captured(1).toLatin1() + '-' +
                              match.captured(2).toLatin1() + ";server_port=" + QByteArray::number(port) + '-' +
                              QByteArray::number(port + 1) + ";source=127.0.0.1\r\n");
                }
            } else if (method == "PLAY") {
                _played = true;
                reply(sequence, 200, "Range: npt=0.000-\r\n");
                _video.start(_destination, _tcpOnly ? _client.data() : nullptr, quint8(_destination));
            } else {
                reply(sequence, 200);
            }
        }
    }

    bool _tcpOnly = false;
    bool _played = false;
    quint16 _destination = 0;
    QTcpServer _server;
    QPointer<QTcpSocket> _client;
    QByteArray _requests;
    QStringList _transports;
    RtpVideoFixture _video;
};
}  // namespace

void FFmpegVideoReceiverTest::invalidUri()
{
    FFmpegVideoReceiver receiver;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    receiver.start(3);
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_INVALID_URL);
}

void FFmpegVideoReceiverTest::cancelPendingStream()
{
    FFmpegVideoReceiver receiver;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.setUri(QStringLiteral("udp://127.0.0.1:56991"));
    receiver.start(30);
    QCOMPARE(started.count(), 0);
    QElapsedTimer elapsed;
    elapsed.start();
    receiver.stop();
    QVERIFY(elapsed.elapsed() < 100);
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    QCOMPARE(stopped.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    for (const auto& result : started) {
        QVERIFY(result.at(0).value<VideoReceiver::STATUS>() != VideoReceiver::STATUS_OK);
    }
}

void FFmpegVideoReceiverTest::decodeRecordScreenshot()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    const QString recording = scratch.filePath(QStringLiteral("recording.mp4"));
    const QString screenshot = scratch.filePath(QStringLiteral("frame.png"));
    FFmpegVideoReceiver receiver;
    QVideoSink sink;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy recordingStarted(&receiver, &VideoReceiver::onStartRecordingComplete);
    QSignalSpy recordingStopped(&receiver, &VideoReceiver::onStopRecordingComplete);
    QSignalSpy screenshotTaken(&receiver, &VideoReceiver::onTakeScreenshotComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.startDecoding(&sink);
    receiver.setUri(QUrl::fromLocalFile(input).toString());
    receiver.start(3);
    QCOMPARE(started.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 4000);
    QCOMPARE(started.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 1000);
    QCOMPARE(sink.videoSize(), QSize(160, 120));
    {
        QVideoFrame frame = sink.videoFrame();
        QCOMPARE(frame.pixelFormat(), QVideoFrameFormat::Format_YUV420P);
        QVERIFY(frame.map(QVideoFrame::ReadOnly));
        const auto unmap = qScopeGuard([&frame]() { frame.unmap(); });
        QVERIFY(frame.isReadable());
        QCOMPARE(frame.planeCount(), 3);
        for (int plane = 0; plane < 3; ++plane) {
            const int width = plane == 0 ? 160 : 80;
            const int height = plane == 0 ? 120 : 60;
            QVERIFY(frame.bits(plane));
            QVERIFY(frame.bytesPerLine(plane) >= width);
            QVERIFY(frame.mappedBytes(plane) >= frame.bytesPerLine(plane) * (height - 1) + width);
        }
    }
    receiver.takeScreenshot(screenshot);
    QCOMPARE(screenshotTaken.count(), 1);
    QCOMPARE(screenshotTaken.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QImage image(screenshot);
    QCOMPARE(image.size(), QSize(160, 120));
    QVERIFY(image.pixelColor(80, 60) != image.pixelColor(0, 0));
    receiver.startRecording(recording, VideoReceiver::FILE_FORMAT_MP4);
    QTRY_COMPARE_WITH_TIMEOUT(recordingStarted.count(), 1, 2500);
    QCOMPARE(recordingStarted.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QSignalSpy recordedFrames(&sink, &QVideoSink::videoFrameChanged);
    QTRY_VERIFY_WITH_TIMEOUT(recordedFrames.count() >= 5, 1500);
    receiver.stopRecording();
    QTRY_COMPARE_WITH_TIMEOUT(recordingStopped.count(), 1, 1000);
    QCOMPARE(recordingStopped.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QSignalSpy decodingStopped(&receiver, &VideoReceiver::onStopDecodingComplete);
    receiver.stopDecoding();
    QCOMPARE(decodingStopped.count(), 1);
    QCOMPARE(decodingStopped.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(!sink.videoFrame().isValid());
    recordedFrames.clear();
    QVERIFY2(!recordedFrames.wait(350), "A detached sink must not receive subsequent decoded frames");
    QVERIFY(receiver.running());
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    QVERIFY(QFile(recording).size() > 512);
    FFmpegVideoReceiver replay;
    QVideoSink replaySink;
    QSignalSpy replayStarted(&replay, &VideoReceiver::onStartComplete);
    replay.startDecoding(&replaySink);
    replay.setUri(QUrl::fromLocalFile(recording).toString());
    replay.start(3);
    QTRY_COMPARE_WITH_TIMEOUT(replayStarted.count(), 1, 4000);
    QCOMPARE(replayStarted.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_COMPARE_WITH_TIMEOUT(replaySink.videoSize(), QSize(160, 120), 1000);
    replay.stop();
}

void FFmpegVideoReceiverTest::destroyedSinkAllowsRebinding()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    FFmpegVideoReceiver receiver;
    auto sink = std::make_unique<QVideoSink>();
    QPointer<QVideoSink> originalSink(sink.get());
    QSignalSpy frames(sink.get(), &QVideoSink::videoFrameChanged);
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.startDecoding(sink.get());
    receiver.setUri(QUrl::fromLocalFile(input).toString());
    receiver.start(3);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 4000);
    QCOMPARE(started.first().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 3, 1500);
    sink.reset();
    QVERIFY(originalSink.isNull());
    QVERIFY2(!stopped.wait(250), "Destroying the sink must leave the stream active");
    QVERIFY(receiver.running());

    QVideoSink replacement;
    QSignalSpy replacementFrames(&replacement, &QVideoSink::videoFrameChanged);
    receiver.startDecoding(&replacement);
    QTRY_VERIFY_WITH_TIMEOUT(replacementFrames.count() >= 3, 1500);
    QVERIFY(replacement.videoFrame().isValid());
    QCOMPARE(replacement.videoSize(), QSize(160, 120));
    QCOMPARE(started.count(), 1);
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    QCOMPARE(stopped.first().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(!receiver.running());
    QVERIFY(!replacement.videoFrame().isValid());
}

void FFmpegVideoReceiverTest::rtpH264Loopback()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    RtpVideoFixture fixture;
    QVERIFY(fixture.load(input));
    FFmpegVideoReceiver receiver;
    QVideoSink sink;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.startDecoding(&sink);
    receiver.setUri(QStringLiteral("udp://127.0.0.1:56992"));
    receiver.start(5);
    fixture.start(56992);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 7000);
    QCOMPARE(started.takeFirst().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_COMPARE_WITH_TIMEOUT(sink.videoSize(), QSize(160, 120), 1000);
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
}

void FFmpegVideoReceiverTest::rtspTransportNegotiation_data()
{
    QTest::addColumn<bool>("tcpOnly");
    QTest::newRow("udp-only") << false;
    QTest::newRow("tcp-only") << true;
}

void FFmpegVideoReceiverTest::rtspTransportNegotiation()
{
    QFETCH(bool, tcpOnly);
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    RtspVideoFixture fixture(tcpOnly);
    QVERIFY(fixture.start(input));
    FFmpegVideoReceiver receiver;
    QVideoSink sink;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.startDecoding(&sink);
    receiver.setUri(fixture.uri());
    receiver.start(5);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 7000);
    const auto status = started.first().at(0).value<VideoReceiver::STATUS>();
    QVERIFY2(status == VideoReceiver::STATUS_OK, qPrintable(fixture.transports().join(QStringLiteral(" | "))));
    QVERIFY(fixture.played());
    QTRY_COMPARE_WITH_TIMEOUT(sink.videoSize(), QSize(160, 120), 1500);
    QVERIFY(sink.videoFrame().isValid());
    QVERIFY(!fixture.transports().isEmpty());
    QCOMPARE(fixture.transports().last().contains(QStringLiteral("RTP/AVP/TCP")), tcpOnly);
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
}

#ifndef QGC_FFMPEG_NATIVE_TEST
void FFmpegVideoReceiverTest::badMediaFailsToStart()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    QFile input(scratch.filePath(QStringLiteral("invalid.mp4")));
    QVERIFY(input.open(QIODevice::WriteOnly));
    const QByteArray contents("This file contains no encoded video or media container.");
    QCOMPARE(input.write(contents), qint64(contents.size()));
    input.close();

    FFmpegVideoReceiver receiver;
    QVideoSink sink;
    QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.startDecoding(&sink);
    receiver.setUri(QUrl::fromLocalFile(input.fileName()).toString());
    expectLogMessage("default", QtWarningMsg, QRegularExpression(QStringLiteral("FFmpeg video source failed:.*")));
    receiver.start(3);
    QCOMPARE(started.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 4000);
    verifyExpectedLogMessage();
    QCOMPARE(started.first().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_FAIL);
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    QCOMPARE(stopped.first().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_FAIL);
    QVERIFY(!receiver.running());
    QVERIFY(!sink.videoFrame().isValid());
    QCOMPARE(frames.count(), 0);
}

void FFmpegVideoReceiverTest::disabledStreamDoesNotRestart()
{
    _verifyManagerStop(true);
}

void FFmpegVideoReceiverTest::explicitStopDoesNotRestart()
{
    _verifyManagerStop(false);
}

void FFmpegVideoReceiverTest::disabledFactStopsVideo()
{
    _verifyManagerStop(true, false);
}

void FFmpegVideoReceiverTest::_verifyManagerStop(bool disableStream, bool explicitStop)
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    VideoSettings* settings = SettingsManager::instance()->videoSettings();
    const QVariant previousSource = settings->videoSource()->rawValue();
    const QVariant previousUrl = settings->rtspUrl()->rawValue();
    const QVariant previousEnabled = settings->streamEnabled()->rawValue();
    const auto restoreSettings = qScopeGuard([settings, previousSource, previousUrl, previousEnabled]() {
        settings->videoSource()->setRawValue(previousSource);
        settings->rtspUrl()->setRawValue(previousUrl);
        settings->streamEnabled()->setRawValue(previousEnabled);
    });
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoSourceRTSP));
    settings->rtspUrl()->setRawValue(QUrl::fromLocalFile(input).toString());
    settings->streamEnabled()->setRawValue(true);
    VideoManager manager;
    QQuickWindow window;
    QVideoSink sink;
    QQuickItem widget(window.contentItem());
    widget.setObjectName(QStringLiteral("videoContent"));
    widget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&sink)));
    auto* receiver = new FFmpegVideoReceiver(&manager);
    receiver->setName(QStringLiteral("videoContent"));
    QSignalSpy started(receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(receiver, &VideoReceiver::onStopComplete);
    manager._initVideoReceiver(receiver, &window);
    QTRY_VERIFY_WITH_TIMEOUT(receiver->started(), 4000);
    QCOMPARE(started.count(), 1);
    if (disableStream) {
        settings->streamEnabled()->setRawValue(false);
    }
    if (explicitStop) {
        manager.stopVideo();
    }
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    QVERIFY2(!started.wait(1500), "Stopped video must not restart");
    QVERIFY(!receiver->running());
    QCOMPARE(started.count(), 1);
    QCOMPARE(settings->streamEnabled()->rawValue().toBool(), !disableStream);
}

void FFmpegVideoReceiverTest::sourceDisabledThenEnabledStarts()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    VideoSettings* settings = SettingsManager::instance()->videoSettings();
    const QVariant previousSource = settings->videoSource()->rawValue();
    const QVariant previousUrl = settings->rtspUrl()->rawValue();
    const QVariant previousEnabled = settings->streamEnabled()->rawValue();
    const auto restoreSettings = qScopeGuard([settings, previousSource, previousUrl, previousEnabled]() {
        settings->videoSource()->setRawValue(previousSource);
        settings->rtspUrl()->setRawValue(previousUrl);
        settings->streamEnabled()->setRawValue(previousEnabled);
    });
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoDisabled));
    settings->rtspUrl()->setRawValue(QString());
    settings->streamEnabled()->setRawValue(true);
    VideoManager manager;
    QQuickWindow window;
    QVideoSink sink;
    QQuickItem widget(window.contentItem());
    widget.setObjectName(QStringLiteral("videoContent"));
    widget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&sink)));
    auto* receiver = new FFmpegVideoReceiver(&manager);
    receiver->setName(QStringLiteral("videoContent"));
    QSignalSpy started(receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(receiver, &VideoReceiver::onStopComplete);
    manager._initVideoReceiver(receiver, &window);
    manager.stopVideo();
    manager.startVideo();
    manager._videoSourceChanged();
    QCOMPARE(started.count(), 0);
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoSourceRTSP));
    manager._videoSourceChanged();
    QCOMPARE(started.count(), 0);
    settings->rtspUrl()->setRawValue(QUrl::fromLocalFile(input).toString());
    manager._videoSourceChanged();
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 4000);
    QVERIFY(receiver->started());
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoDisabled));
    manager._videoSourceChanged();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
    settings->rtspUrl()->setRawValue(QString());
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoSourceRTSP));
    manager._videoSourceChanged();
    QVERIFY(!receiver->running());
    settings->rtspUrl()->setRawValue(QUrl::fromLocalFile(input).toString());
    manager._videoSourceChanged();
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 4000);
    QVERIFY(receiver->started());
    manager.stopVideo();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 2, 2000);
}

void FFmpegVideoReceiverTest::autoReconnectPolicyAfterTransportLoss_data()
{
    QTest::addColumn<bool>("disableDuringBackoff");
    QTest::addColumn<bool>("keepAutoReconnectEnabled");
    QTest::newRow("disabled-before-transport-failure") << false << false;
    QTest::newRow("disabled-during-backoff") << true << false;
    QTest::newRow("enabled-transport-recovery") << false << true;
}

void FFmpegVideoReceiverTest::autoReconnectPolicyAfterTransportLoss()
{
    QFETCH(bool, disableDuringBackoff);
    QFETCH(bool, keepAutoReconnectEnabled);
    ignoreLogMessage("default", QtWarningMsg, QRegularExpression(QStringLiteral("FFmpeg video source failed:.*")));
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    RtspVideoFixture fixture(true);
    QVERIFY(fixture.start(input));
    VideoSettings* settings = SettingsManager::instance()->videoSettings();
    const QVariant previousSource = settings->videoSource()->rawValue();
    const QVariant previousUrl = settings->rtspUrl()->rawValue();
    const QVariant previousEnabled = settings->streamEnabled()->rawValue();
    const QVariant previousReconnect = settings->rtspAutoReconnect()->rawValue();
    const auto restoreSettings = qScopeGuard([=]() {
        settings->videoSource()->setRawValue(previousSource);
        settings->rtspUrl()->setRawValue(previousUrl);
        settings->streamEnabled()->setRawValue(previousEnabled);
        settings->rtspAutoReconnect()->setRawValue(previousReconnect);
    });
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoSourceRTSP));
    settings->rtspUrl()->setRawValue(fixture.uri());
    settings->streamEnabled()->setRawValue(true);
    settings->rtspAutoReconnect()->setRawValue(disableDuringBackoff || keepAutoReconnectEnabled);
    VideoManager manager;
    QQuickWindow window;
    QVideoSink sink;
    QQuickItem widget(window.contentItem());
    widget.setObjectName(QStringLiteral("videoContent"));
    widget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&sink)));
    auto* receiver = new FFmpegVideoReceiver(&manager);
    receiver->setName(QStringLiteral("videoContent"));
    QSignalSpy started(receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(receiver, &VideoReceiver::onStopComplete);
    manager._initVideoReceiver(receiver, &window);
    QTRY_VERIFY_WITH_TIMEOUT(receiver->started(), 7000);
    QCOMPARE(receiver->autoReconnect(), disableDuringBackoff || keepAutoReconnectEnabled);
    if (disableDuringBackoff) {
        // Manager schedules its retry first; this cancels the live receiver setting before it fires.
        connect(receiver, &VideoReceiver::onStopComplete, receiver,
                [receiver]() { receiver->setAutoReconnect(false); });
    }
    if (keepAutoReconnectEnabled) {
        fixture.disconnectClient();
    } else {
        fixture.stop();
    }
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 7000);
    if (keepAutoReconnectEnabled) {
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 7000);
        QCOMPARE(started.last().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
        QVERIFY(receiver->started());
        manager.stopVideo();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 2, 3000);
    }
    QVERIFY2(!started.wait(1500), "Disabled automatic reconnect must not reopen a failed transport");
    QVERIFY(!receiver->running());
    QCOMPARE(started.count(), keepAutoReconnectEnabled ? 2 : 1);
}

void FFmpegVideoReceiverTest::autoReconnectDisabledAllowsExplicitRestart_data()
{
    QTest::addColumn<bool>("changeSource");
    QTest::newRow("user-restart") << false;
    QTest::newRow("URI-change") << true;
}

void FFmpegVideoReceiverTest::autoReconnectDisabledAllowsExplicitRestart()
{
    QFETCH(bool, changeSource);
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
    RtspVideoFixture fixture(true);
    RtspVideoFixture replacement(true);
    QVERIFY(fixture.start(input));
    QVERIFY(replacement.start(input));
    VideoSettings* settings = SettingsManager::instance()->videoSettings();
    const QVariant previousSource = settings->videoSource()->rawValue();
    const QVariant previousUrl = settings->rtspUrl()->rawValue();
    const QVariant previousEnabled = settings->streamEnabled()->rawValue();
    const QVariant previousReconnect = settings->rtspAutoReconnect()->rawValue();
    const auto restoreSettings = qScopeGuard([=]() {
        settings->videoSource()->setRawValue(previousSource);
        settings->rtspUrl()->setRawValue(previousUrl);
        settings->streamEnabled()->setRawValue(previousEnabled);
        settings->rtspAutoReconnect()->setRawValue(previousReconnect);
    });
    settings->videoSource()->setRawValue(QString::fromLatin1(VideoSettings::videoSourceRTSP));
    settings->rtspUrl()->setRawValue(fixture.uri());
    settings->streamEnabled()->setRawValue(true);
    settings->rtspAutoReconnect()->setRawValue(false);
    VideoManager manager;
    QQuickWindow window;
    QVideoSink sink;
    QQuickItem widget(window.contentItem());
    widget.setObjectName(QStringLiteral("videoContent"));
    widget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&sink)));
    auto* receiver = new FFmpegVideoReceiver(&manager);
    receiver->setName(QStringLiteral("videoContent"));
    QSignalSpy started(receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(receiver, &VideoReceiver::onStopComplete);
    manager._initVideoReceiver(receiver, &window);
    QTRY_VERIFY_WITH_TIMEOUT(receiver->started(), 7000);
    QVERIFY(!receiver->autoReconnect());
    if (changeSource) {
        settings->rtspUrl()->setRawValue(replacement.uri());
        manager._videoSourceChanged();
    } else {
        manager.startVideo();
    }
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 7000);
    QCOMPARE(started.last().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(receiver->started());
    if (changeSource) {
        QCOMPARE(receiver->uri(), replacement.uri());
    }
    manager.stopVideo();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 2, 3000);
    QVERIFY2(!started.wait(1500), "Explicit stop must cancel user-requested restarts");
    QVERIFY(!receiver->running());
}
#endif

#ifdef QGC_FFMPEG_NATIVE_TEST
QTEST_MAIN(FFmpegVideoReceiverTest)
#else
UT_REGISTER_TEST(FFmpegVideoReceiverTest, TestLabel::Integration)
#endif
