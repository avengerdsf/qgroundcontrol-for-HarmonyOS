#pragma once

#include "VideoReceiver.h"

#include <QtCore/QPointer>
#include <QtMultimedia/QVideoFrame>
#include <memory>

class QThread;
class QVideoSink;

class FFmpegVideoReceiver : public VideoReceiver
{
    Q_OBJECT

public:
    explicit FFmpegVideoReceiver(QObject *parent = nullptr);
    ~FFmpegVideoReceiver() override;

    static bool enabled();
    static void *createVideoSink(QQuickItem *widget, QObject *parent = nullptr);
    static void releaseVideoSink(void *sink);
    static VideoReceiver *createVideoReceiver(QObject *parent);
    bool running() const { return bool(_state); }

public slots:
    void start(uint32_t timeout) override;
    void stop() override;
    void startDecoding(void *sink) override;
    void stopDecoding() override;
    void startRecording(const QString &videoFile, FILE_FORMAT format) override;
    void stopRecording() override;
    void takeScreenshot(const QString &imageFile) override;

private:
    struct StreamState;
    void _receiveFrame(const std::shared_ptr<StreamState> &state);
    void _finishStream(const std::shared_ptr<StreamState> &state);
    void _setDecoding(bool active);
    void _setRecording(bool active);

    QThread *_thread = nullptr;
    QPointer<QVideoSink> _videoSink;
    std::shared_ptr<StreamState> _state;
    QVideoFrame _lastFrame;
    QSize _videoSize;
    bool _startPending = false;
    bool _stopPending = false;
    bool _recordStartPending = false;
    bool _recordStopPending = false;
};
