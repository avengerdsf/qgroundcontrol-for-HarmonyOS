#include "VideoManagerInitTest.h"

#if defined(QGC_FFMPEG_STREAMING) && !defined(QGC_GST_STREAMING)
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtMultimedia/QVideoSink>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <memory>

#include "FFmpegVideoReceiver.h"
#include "Fact.h"
#include "SettingsManager.h"
#include "VideoBackend.h"
#include "VideoManager.h"
#include "VideoSettings.h"
#endif

#ifdef QGC_GST_STREAMING

#include <QtCore/QRegularExpression>
#include <QtQuick/QQuickWindow>

#include "VideoManager.h"

void VideoManagerInitTest::init()
{
    UnitTest::init();

    static const QRegularExpression sGStreamerCriticalRe(
        QStringLiteral("cannot register existing type|"
                       "g_type_add_interface_static.*G_TYPE_IS_INSTANTIATABLE|"
                       "g_once_init_leave.*result != 0|"
                       "GStreamer initialization failed"));
    // These backend startup diagnostics are platform/runtime dependent and not
    // tied to a single deterministic call site in this fixture.
    ignoreLogMessage("Video.GStreamer.GStreamerLogging", QtCriticalMsg, sGStreamerCriticalRe);
}

void VideoManagerInitTest::_testQmlReadyBeforeBackendReady()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() { ++createReceiversCount; };

    videoManager._initState = VideoManager::InitState::Pending;

    videoManager._initAfterQmlIsReady();
    QCOMPARE(videoManager._initState, VideoManager::InitState::QmlReady);
    QCOMPARE(createReceiversCount, 0);

    videoManager._onBackendInitComplete(true);
    QCOMPARE(videoManager._initState, VideoManager::InitState::Running);
    QCOMPARE(createReceiversCount, 1);

    expectLogMessage("Video.VideoManager", QtWarningMsg,
                     QRegularExpression(QStringLiteral("_onBackendInitComplete: unexpected state")));
    videoManager._onBackendInitComplete(true);
    verifyExpectedLogMessage();
    QCOMPARE(createReceiversCount, 1);
}

void VideoManagerInitTest::_testBackendReadyBeforeQmlReady()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() { ++createReceiversCount; };

    videoManager._initState = VideoManager::InitState::Pending;

    videoManager._onBackendInitComplete(true);
    QCOMPARE(videoManager._initState, VideoManager::InitState::BackendReady);
    QCOMPARE(createReceiversCount, 0);

    videoManager._initAfterQmlIsReady();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Running);
    QCOMPARE(createReceiversCount, 1);

    expectLogMessage("Video.VideoManager", QtWarningMsg,
                     QRegularExpression(QStringLiteral("_initAfterQmlIsReady: unexpected state")));
    videoManager._initAfterQmlIsReady();
    verifyExpectedLogMessage();
    QCOMPARE(createReceiversCount, 1);
}

void VideoManagerInitTest::_testBackendInitFailure()
{
    VideoManager videoManager;
    QQuickWindow mainWindow;
    videoManager._mainWindow = &mainWindow;

    int createReceiversCount = 0;
    videoManager._createVideoReceiversForTest = [&createReceiversCount]() { ++createReceiversCount; };

    videoManager._initState = VideoManager::InitState::Pending;

    expectLogMessage("Video.VideoManager", QtCriticalMsg,
                     QRegularExpression(QStringLiteral("video initialization failed")));
    videoManager._onBackendInitComplete(false);
    verifyExpectedLogMessage();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Failed);
    QCOMPARE(createReceiversCount, 0);

    expectLogMessage("Video.VideoManager", QtWarningMsg,
                     QRegularExpression(QStringLiteral("QML ready but video init failed")));
    videoManager._initAfterQmlIsReady();
    verifyExpectedLogMessage();
    QCOMPARE(videoManager._initState, VideoManager::InitState::Failed);
    QCOMPARE(createReceiversCount, 0);
}

#else

void VideoManagerInitTest::init()
{
    UnitTest::init();
}

void VideoManagerInitTest::_testQmlReadyBeforeBackendReady()
{
    QSKIP("GStreamer not enabled");
}

void VideoManagerInitTest::_testBackendReadyBeforeQmlReady()
{
    QSKIP("GStreamer not enabled");
}

void VideoManagerInitTest::_testBackendInitFailure()
{
    QSKIP("GStreamer not enabled");
}

#endif

#if defined(QGC_FFMPEG_STREAMING) && !defined(QGC_GST_STREAMING)
void VideoManagerInitTest::_testSynchronousBackendCreatesReceiversOnce()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString input = scratch.filePath(QStringLiteral("fixture.mp4"));
    QVERIFY(QFile::copy(QStringLiteral(":/ffmpeg-test/fixture.mp4"), input));
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
    settings->rtspUrl()->setRawValue(QUrl::fromLocalFile(input).toString());
    settings->streamEnabled()->setRawValue(true);
    settings->rtspAutoReconnect()->setRawValue(false);

    QQuickWindow mainWindow;
    QVideoSink sink;
    QVideoSink thermalSink;
    QQuickItem widget(mainWindow.contentItem());
    widget.setObjectName(QStringLiteral("videoContent"));
    widget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&sink)));
    QQuickItem thermalWidget(mainWindow.contentItem());
    thermalWidget.setObjectName(QStringLiteral("thermalVideo"));
    thermalWidget.setProperty("videoSink", QVariant::fromValue(static_cast<QObject*>(&thermalSink)));
    auto videoManager = std::make_unique<VideoManager>();
    QVERIFY(!VideoBackend::needsAsyncInit());
    QVERIFY(videoManager->waitForVideoBackendReady(std::chrono::milliseconds(0)));
    videoManager->init(&mainWindow);
    videoManager->_initAfterQmlIsReady();
    QCOMPARE(videoManager->_videoReceivers.size(), 2);
    const QList<VideoReceiver*> receivers = videoManager->_videoReceivers;
    QPointer<FFmpegVideoReceiver> primary;
    QPointer<FFmpegVideoReceiver> thermal;
    for (VideoReceiver* receiver : receivers) {
        auto* ffmpeg = qobject_cast<FFmpegVideoReceiver*>(receiver);
        QVERIFY(ffmpeg);
        if (receiver->isThermal()) {
            thermal = ffmpeg;
        } else {
            primary = ffmpeg;
        }
    }
    QVERIFY(primary);
    QVERIFY(thermal);
    QSignalSpy started(primary.data(), &VideoReceiver::onStartComplete);
    QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 4000);
    QCOMPARE(sink.videoSize(), QSize(160, 120));
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.first().at(0).value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(videoManager->streaming());
    QPointer<QThread> worker = primary->findChild<QThread*>();
    QVERIFY(worker);

    videoManager->init(&mainWindow);
    videoManager->_initAfterQmlIsReady();
    QCOMPARE(videoManager->_videoReceivers, receivers);
    QCOMPARE(started.count(), 1);
    videoManager->cleanup();
    videoManager->cleanup();
    QVERIFY(sink.videoFrame().isValid());
    videoManager.reset();
    QVERIFY(primary.isNull());
    QVERIFY(thermal.isNull());
    QVERIFY(worker.isNull());
}
#endif

UT_REGISTER_TEST(VideoManagerInitTest, TestLabel::Unit)
