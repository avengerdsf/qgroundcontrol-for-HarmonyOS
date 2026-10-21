#include "GStreamerVideoReceiverTest.h"
#include "GStreamer.h"
#include "GstVideoReceiver.h"
#include "QGCCorePlugin.h"
#include "VideoManager.h"

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

// gio.h declares a struct member named "signals", which Qt's keyword macro would rewrite.
#ifdef signals
#undef signals
#endif
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gio/gio.h>

#include <atomic>
#include <memory>

namespace {
quint16 unusedUdpPort()
{
    QUdpSocket socket;
    return socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)) ? socket.localPort() : 0;
}

QString videoEncoder(bool h265 = false)
{
    const QString encoder = h265
        ? QStringLiteral("x265enc tune=zerolatency speed-preset=ultrafast key-int-max=15 bitrate=128 option-string=pools=1:frame-threads=1 ! h265parse")
        : QStringLiteral("x264enc tune=zerolatency speed-preset=ultrafast key-int-max=15 bitrate=128 ! h264parse");
    return QStringLiteral("videotestsrc name=picture is-live=true pattern=red ! "
        "video/x-raw,width=160,height=120,framerate=15/1 ! %1").arg(encoder);
}

QString udpSender(quint16 port, const QString &mode)
{
    const QString payload = mode == QStringLiteral("mpegts")
        ? QStringLiteral("mpegtsmux alignment=7")
        : QStringLiteral("rtp%1pay pt=96 config-interval=1").arg(mode == QStringLiteral("udp265") ? QStringLiteral("h265") : QStringLiteral("h264"));
    return QStringLiteral("%1 ! %2 ! udpsink host=127.0.0.1 port=%3 sync=false async=false")
        .arg(videoEncoder(mode == QStringLiteral("udp265")), payload).arg(port);
}

class Pipeline
{
public:
    ~Pipeline() { stop(); }

    bool start(const QString &launch)
    {
        stop();
        GError *error = nullptr;
        _pipeline = gst_parse_launch(launch.toUtf8().constData(), &error);
        if (error) {
            _error = QString::fromUtf8(error->message);
            g_clear_error(&error);
            stop();
            return false;
        }
        if (!_pipeline) {
            _error = QStringLiteral("gst_parse_launch returned null");
            return false;
        }
        if (gst_element_set_state(_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            _error = QStringLiteral("Sender failed to enter PLAYING");
            return false;
        }
        return true;
    }

    void stop()
    {
        if (_pipeline) {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_clear_object(&_pipeline);
        }
    }

    void setPattern(int pattern)
    {
        GstElement *source = gst_bin_get_by_name(GST_BIN(_pipeline), "picture");
        if (source) {
            g_object_set(source, "pattern", pattern, nullptr);
            gst_object_unref(source);
        }
    }

    QString error()
    {
        if (!_pipeline) {
            return _error;
        }
        GstBus *bus = gst_element_get_bus(_pipeline);
        while (GstMessage *message = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING))) {
            GError *error = nullptr;
            gchar *detail = nullptr;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                gst_message_parse_error(message, &error, &detail);
            } else {
                gst_message_parse_warning(message, &error, &detail);
            }
            _error += QStringLiteral("%1: %2 (%3)\n").arg(QString::fromUtf8(GST_OBJECT_NAME(message->src)),
                QString::fromUtf8(error ? error->message : "unknown error"), QString::fromUtf8(detail ? detail : ""));
            g_clear_error(&error);
            g_free(detail);
            gst_message_unref(message);
        }
        gst_object_unref(bus);
        return _error;
    }

private:
    GstElement *_pipeline = nullptr;
    QString _error;
};

class VideoSession
{
public:
    VideoSession()
        : _component(&_engine), receiver(std::make_unique<GstVideoReceiver>()),
          started(receiver.get(), &VideoReceiver::onStartComplete),
          stopped(receiver.get(), &VideoReceiver::onStopComplete),
          decoding(receiver.get(), &VideoReceiver::decodingChanged)
    {
        // A nonempty URI also makes early test-failure teardown well-defined.
        receiver->setUri(QStringLiteral("udp://127.0.0.1:1"));
    }

    ~VideoSession()
    {
        receiver.reset();
        if (_sink) {
            gst_element_set_state(_sink, GST_STATE_NULL);
            gst_object_unref(_sink);
        }
        _item.reset();
        _window.close();
    }

    bool open()
    {
        _engine.addImportPath(QStringLiteral("qrc:/qml"));
        _component.setData(R"(
            import QtQuick
            import QGroundControl.FlightDisplay
            QGCVideoBackground { width: 320; height: 240; forceAspectRatio: true }
        )", QUrl(QStringLiteral("qrc:/gstreamer-video-test.qml")));
        _item.reset(qobject_cast<QQuickItem*>(_component.create()));
        if (!_item) {
            error = _component.errorString();
            return false;
        }
        _item->setParentItem(_window.contentItem());
        _window.setColor(Qt::black);
        _window.resize(320, 240);
        _window.show();
        if (!QTest::qWaitForWindowExposed(&_window, 5000) ||
            !QTest::qWaitFor([this]() { return _item->property("itemInitialized").toBool(); }, 5000)) {
            error = QStringLiteral("GstGLQt6VideoItem did not initialize in the real Qt window");
            return false;
        }
        if (_window.rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL) {
            error = QStringLiteral("qml6glsink requires the Qt Quick OpenGL renderer");
            return false;
        }
        _sink = GST_ELEMENT(QGCCorePlugin::instance()->createVideoSink(_item.get(), nullptr));
        if (!_sink) {
            error = QStringLiteral("qgcvideosinkbin creation failed");
            return false;
        }
        gst_object_ref_sink(_sink);
        GstPad *pad = gst_element_get_static_pad(_sink, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, [](GstPad*, GstPadProbeInfo*, gpointer data) {
            static_cast<VideoSession*>(data)->frames.fetch_add(1);
            return GST_PAD_PROBE_OK;
        }, this, nullptr);
        gst_object_unref(pad);
        receiver->setWidget(_item.get());
        receiver->setSink(_sink);
        return true;
    }

    void start(const QString &uri, bool lowLatency = false)
    {
        receiver->setUri(uri);
        receiver->setLowLatency(lowLatency);
        receiver->startDecoding(_sink);
        receiver->start(5);
    }

    QImage grab()
    {
        auto result = _item->grabToImage();
        if (!result) {
            return {};
        }
        QSignalSpy ready(result.get(), &QQuickItemGrabResult::ready);
        if (result->image().isNull() && !ready.wait(2000)) {
            return {};
        }
        return result->image();
    }

    bool displays(Qt::GlobalColor expected)
    {
        const QImage image = grab();
        if (image.isNull()) {
            return false;
        }
        const QColor color = image.pixelColor(image.width() / 2, image.height() / 2);
        return expected == Qt::red ? color.red() > 150 && color.green() < 80 && color.blue() < 80
                                   : color.blue() > 150 && color.red() < 80 && color.green() < 80;
    }

    QString error;
    std::atomic<int> frames{0};
    GstElement *sink() const { return _sink; }

private:
    QQuickWindow _window;
    QQmlEngine _engine;
    QQmlComponent _component;
    std::unique_ptr<QQuickItem> _item;
    GstElement *_sink = nullptr;

public:
    std::unique_ptr<GstVideoReceiver> receiver;
    QSignalSpy started;
    QSignalSpy stopped;
    QSignalSpy decoding;
};

class RtspServer
{
public:
    explicit RtspServer(bool tcp, GTlsCertificate *certificate = nullptr)
    {
        _context = g_main_context_new();
        _server = gst_rtsp_server_new();
        gst_rtsp_server_set_address(_server, "127.0.0.1");
        gst_rtsp_server_set_service(_server, "0");
        GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(_server);
        GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
        if (certificate) {
            GstRTSPAuth *auth = gst_rtsp_auth_new();
            gst_rtsp_auth_set_tls_certificate(auth, certificate);
            GstRTSPToken *token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "test", nullptr);
            gst_rtsp_auth_set_default_token(auth, token);
            gst_rtsp_token_unref(token);
            gst_rtsp_server_set_auth(_server, auth);
            g_object_unref(auth);
            gst_rtsp_media_factory_add_role(factory, "test", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, nullptr);
            _secure = true;
        }
        const QString launch = QStringLiteral("( %1 ! rtph264pay name=pay0 pt=96 config-interval=1 )").arg(videoEncoder());
        gst_rtsp_media_factory_set_launch(factory, launch.toUtf8().constData());
        gst_rtsp_media_factory_set_protocols(factory, tcp ? GST_RTSP_LOWER_TRANS_TCP : GST_RTSP_LOWER_TRANS_UDP);
        gst_rtsp_mount_points_add_factory(mounts, "/video", factory);
        g_object_unref(mounts);
        g_signal_connect(_server, "client-connected", G_CALLBACK(+[](GstRTSPServer*, GstRTSPClient *client, gpointer data) {
            g_signal_connect(client, "setup-request", G_CALLBACK(+[](GstRTSPClient*, GstRTSPContext *context, gpointer data) {
                const GstRTSPTransport *transport = context->trans ? gst_rtsp_stream_transport_get_transport(context->trans) : nullptr;
                if (transport) {
                    static_cast<RtspServer*>(data)->transport.store(transport->lower_transport);
                }
            }), data);
        }), this);
        _source = gst_rtsp_server_attach(_server, _context);
        QObject::connect(&_timer, &QTimer::timeout, &_timer, [this]() {
            while (g_main_context_iteration(_context, FALSE)) {}
        });
        _timer.start(5);
    }

    ~RtspServer()
    {
        _timer.stop();
        gst_rtsp_server_client_filter(_server, [](GstRTSPServer*, GstRTSPClient*, gpointer) {
            return GST_RTSP_FILTER_REMOVE;
        }, nullptr);
        if (_source) {
            g_source_destroy(g_main_context_find_source_by_id(_context, _source));
        }
        g_object_unref(_server);
        g_main_context_unref(_context);
    }

    QString uri() const
    {
        return _source ? QStringLiteral("%1://127.0.0.1:%2/video")
            .arg(_secure ? QStringLiteral("rtsps") : QStringLiteral("rtsp"))
            .arg(gst_rtsp_server_get_bound_port(_server)) : QString();
    }

    std::atomic<int> transport{0};

private:
    GstRTSPServer *_server = nullptr;
    GMainContext *_context = nullptr;
    guint _source = 0;
    QTimer _timer;
    bool _secure = false;
};
} // namespace

void GStreamerVideoReceiverTest::initTestCase()
{
    if (!gst_is_initialized()) {
        QVERIFY2(GStreamer::initialize(), "Production GStreamer initialization failed");
    }
    qRegisterMetaType<VideoReceiver::STATUS>();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
}

void GStreamerVideoReceiverTest::registryAndFactories()
{
    QVERIFY(VideoManager::gstreamerEnabled());
    QVERIFY(!VideoManager::ffmpegEnabled());
    QVERIFY(!VideoManager::qtVideoOutputEnabled());
    std::unique_ptr<VideoReceiver> receiver(QGCCorePlugin::instance()->createVideoReceiver(nullptr));
    QVERIFY(qobject_cast<GstVideoReceiver*>(receiver.get()));
    receiver->setUri(QStringLiteral("udp://127.0.0.1:1"));
#ifdef QGC_GST_TEST_PLUGINS
    for (const QString &name : QStringLiteral(QGC_GST_TEST_PLUGINS).split(',')) {
        GstPlugin *plugin = gst_registry_find_plugin(gst_registry_get(), name.toUtf8().constData());
        QVERIFY2(plugin, qPrintable(name));
        QVERIFY2(gst_plugin_get_filename(plugin) == nullptr, qPrintable(name));
        gst_object_unref(plugin);
    }
#endif
    for (const char *name : {"appsrc", "appsink", "videotestsrc", "videoconvert", "videoscale", "decodebin",
             "avdec_h264", "avdec_h265", "openh264enc", "vp8dec", "vp9dec", "h264parse", "h265parse",
             "rtspsrc", "rtpjitterbuffer", "rtph264depay", "rtph265depay", "udpsrc", "tcpclientsrc",
             "tsdemux", "mp4mux", "matroskamux", "glupload", "glcolorconvert", "glsinkbin",
             "qml6glsink", "qgcvideosinkbin", "x264enc", "x265enc", "mpegtsmux"}) {
        GstElementFactory *factory = gst_element_factory_find(name);
        QVERIFY2(factory, name);
        gst_object_unref(factory);
    }
    // A registered factory is not proof that the element can be instantiated: static plugins fail
    // here when their dependency archives were not linked or their plugin init failed.
    for (const char *name : {"avdec_h264", "avdec_h265", "openh264dec", "tsdemux", "h264parse",
             "rtph264depay", "glupload", "glcolorconvert"}) {
        GstElement *element = gst_element_factory_make(name, nullptr);
        QVERIFY2(element, name);
        gst_object_unref(element);
    }
    GstElement *sink = gst_element_factory_make("qgcvideosinkbin", nullptr);
    QVERIFY(sink);
    GstIterator *children = gst_bin_iterate_recurse(GST_BIN(sink));
    GValue value = G_VALUE_INIT;
    QStringList factories;
    while (gst_iterator_next(children, &value) == GST_ITERATOR_OK) {
        auto element = GST_ELEMENT(g_value_get_object(&value));
        GstElementFactory *factory = gst_element_get_factory(element);
        if (factory) {
            factories.append(QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))));
        }
        g_value_reset(&value);
    }
    g_value_unset(&value);
    gst_iterator_free(children);
    gst_object_unref(sink);
    QVERIFY(factories.contains(QStringLiteral("glsinkbin")));
    QVERIFY(factories.contains(QStringLiteral("qml6glsink")));
}

void GStreamerVideoReceiverTest::sandboxEnvironment()
{
#ifdef Q_OS_HARMONY
    QCOMPARE(qgetenv("GST_REGISTRY_FORK"), QByteArray("no"));
    for (const char *name : {"GST_PLUGIN_PATH", "GST_PLUGIN_PATH_1_0", "GST_PLUGIN_SYSTEM_PATH", "GST_PLUGIN_SYSTEM_PATH_1_0"}) {
        QVERIFY2(qEnvironmentVariableIsSet(name) && qEnvironmentVariableIsEmpty(name), name);
    }
    QVERIFY(!qEnvironmentVariableIsSet("GST_PLUGIN_SCANNER"));
    QVERIFY(!qEnvironmentVariableIsSet("GST_PLUGIN_SCANNER_1_0"));
    QCOMPARE(qgetenv("GST_REGISTRY"), qgetenv("GST_REGISTRY_1_0"));
    const QFileInfo registry(qEnvironmentVariable("GST_REGISTRY_1_0"));
    QCOMPARE(registry.absolutePath(), QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).absolutePath());
    QVERIFY(QFileInfo(registry.absolutePath()).isWritable());
    QVERIFY(g_tls_backend_supports_tls(g_tls_backend_get_default()));
    QVERIFY(g_tls_backend_get_default_database(g_tls_backend_get_default()));
    QVERIFY(QFileInfo(qEnvironmentVariable("SSL_CERT_FILE")).size() > 0);
#endif
}

void GStreamerVideoReceiverTest::receiveAndRender_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("RTP-H264") << QStringLiteral("udp");
    QTest::newRow("RTP-H265") << QStringLiteral("udp265");
    QTest::newRow("MPEGTS-UDP") << QStringLiteral("mpegts");
}

void GStreamerVideoReceiverTest::receiveAndRender()
{
    QFETCH(QString, mode);
    const quint16 port = unusedUdpPort();
    QVERIFY(port);
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    session.start(QStringLiteral("%1://127.0.0.1:%2").arg(mode).arg(port));
    Pipeline sender;
    QVERIFY2(sender.start(udpSender(port, mode)), qPrintable(sender.error()));
    QTRY_COMPARE_WITH_TIMEOUT(session.started.count(), 1, 5000);
    QCOMPARE(session.started.first().first().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= 3, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
    sender.setPattern(6); // GstVideoTestSrc blue
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::blue), 3000);
    QVERIFY2(sender.error().isEmpty(), qPrintable(sender.error()));
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 1, 5000);
    QCOMPARE(session.stopped.first().first().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
}

void GStreamerVideoReceiverTest::rtspTransport_data()
{
    QTest::addColumn<bool>("tcp");
    QTest::newRow("RTSP-UDP") << false;
    QTest::newRow("RTSP-TCP") << true;
}

void GStreamerVideoReceiverTest::rtspTransport()
{
    QFETCH(bool, tcp);
    RtspServer server(tcp);
    QVERIFY(!server.uri().isEmpty());
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    session.start(server.uri());
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= 3, 12000);
    QCOMPARE(server.transport.load(), int(tcp ? GST_RTSP_LOWER_TRANS_TCP : GST_RTSP_LOWER_TRANS_UDP));
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 1, 5000);
}

void GStreamerVideoReceiverTest::rtspsTrust_data()
{
    QTest::addColumn<bool>("trusted");
    QTest::newRow("embedded-ca-rejects-self-signed") << false;
    QTest::newRow("test-ca-delivers-video") << true;
}

void GStreamerVideoReceiverTest::rtspsTrust()
{
    QFETCH(bool, trusted);
    QFile certificateFile(trusted ? QStringLiteral(":/GStreamerTests/tls/server-certificate.data")
                                  : QStringLiteral(":/GStreamerTests/tls/self-signed-certificate.data"));
    QFile keyFile(QStringLiteral(":/GStreamerTests/tls/server-public-test-key.data"));
    QVERIFY(certificateFile.open(QIODevice::ReadOnly));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    const QByteArray pem = certificateFile.readAll() + keyFile.readAll();
    GError *error = nullptr;
    const auto errorCleanup = qScopeGuard([&] { g_clear_error(&error); });
    GTlsCertificate *certificate = g_tls_certificate_new_from_pem(pem.constData(), pem.size(), &error);
    const auto certificateCleanup = qScopeGuard([&] { if (certificate) { g_object_unref(certificate); } });
    QVERIFY2(certificate, error ? error->message : "TLS certificate creation failed");
    RtspServer server(true, certificate);
    QVERIFY(!server.uri().isEmpty());
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    GstElement *source = session.receiver->_makeSource(server.uri());
    QVERIFY(source);
    auto sourceCleanup = qScopeGuard([&] { gst_object_unref(source); });
    GstElement *rtsp = gst_bin_get_by_name(GST_BIN(source), "source");
    QVERIFY(rtsp);
    const auto rtspCleanup = qScopeGuard([&] { gst_object_unref(rtsp); });
    guint validationFlags = 0;
    g_object_get(rtsp, "tls-validation-flags", &validationFlags, nullptr);
    QCOMPARE(validationFlags, guint(G_TLS_CERTIFICATE_VALIDATE_ALL));
    QTemporaryDir temporary(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("rtsps-test-XXXXXX")));
    QVERIFY(temporary.isValid());
    GTlsDatabase *database = nullptr;
    const auto databaseCleanup = qScopeGuard([&] { if (database) { g_object_unref(database); } });
    if (trusted) {
        QFile resource(QStringLiteral(":/GStreamerTests/tls/ca-certificate.data"));
        QVERIFY(resource.open(QIODevice::ReadOnly));
        QFile caFile(temporary.filePath(QStringLiteral("ca.pem")));
        QVERIFY(caFile.open(QIODevice::WriteOnly));
        const QByteArray ca = resource.readAll();
        QCOMPARE(caFile.write(ca), ca.size());
        caFile.close();
        database = g_tls_file_database_new(QFile::encodeName(caFile.fileName()).constData(), &error);
        QVERIFY2(database, error ? error->message : "Test CA creation failed");
        g_object_set(rtsp, "tls-database", database, nullptr);
    }
    std::atomic<guint> certificateErrors{0};
    g_signal_connect(rtsp, "accept-certificate", G_CALLBACK(+[](GstElement*, GTlsConnection*, GTlsCertificate*,
            GTlsCertificateFlags errors, gpointer data) -> gboolean {
        static_cast<std::atomic<guint>*>(data)->fetch_or(guint(errors));
        return FALSE;
    }), &certificateErrors);
    GstElement *decoder = session.receiver->_makeDecoder();
    QVERIFY(decoder);
    auto decoderCleanup = qScopeGuard([&] { gst_object_unref(decoder); });
    GstElement *pipeline = gst_pipeline_new("rtsps-receiver-test");
    QVERIFY(pipeline);
    const auto pipelineCleanup = qScopeGuard([&] {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    });
    QVERIFY(gst_bin_add(GST_BIN(pipeline), source));
    sourceCleanup.dismiss();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), decoder));
    decoderCleanup.dismiss();
    GstElement *sink = session.sink();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), sink));
    auto linkPad = +[](GstElement*, GstPad *pad, gpointer target) {
        GstPad *input = gst_element_get_static_pad(GST_ELEMENT(target), "sink");
        if (input) {
            if (!gst_pad_is_linked(input)) { gst_pad_link(pad, input); }
            gst_object_unref(input);
        }
    };
    g_signal_connect(source, "pad-added", G_CALLBACK(linkPad), decoder);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(linkPad), sink);
    GstBus *bus = gst_element_get_bus(pipeline);
    QVERIFY(bus);
    const auto busCleanup = qScopeGuard([&] { gst_object_unref(bus); });
    GstMessage *failure = nullptr;
    const auto failureCleanup = qScopeGuard([&] { if (failure) { gst_message_unref(failure); } });
    QVERIFY(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        if (!failure) { failure = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR); }
        return failure || session.frames.load() >= 3;
    }()), 12000);
    if (trusted) {
        if (failure) {
            gst_message_parse_error(failure, &error, nullptr);
            QFAIL(error ? error->message : "Trusted RTSPS stream failed");
        }
        QCOMPARE(certificateErrors.load(), 0U);
        QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
    } else {
        QVERIFY2(failure, "Untrusted RTSPS server did not produce an error");
        QCOMPARE(session.frames.load(), 0);
        QVERIFY(certificateErrors.load() & G_TLS_CERTIFICATE_UNKNOWN_CA);
        QVERIFY(!(certificateErrors.load() & (G_TLS_CERTIFICATE_BAD_IDENTITY | G_TLS_CERTIFICATE_EXPIRED |
            G_TLS_CERTIFICATE_NOT_ACTIVATED)));
    }
}

void GStreamerVideoReceiverTest::recordAndReplay()
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QVERIFY(QDir().mkpath(cache));
    QTemporaryDir scratch(QDir(cache).filePath(QStringLiteral("gst-video-test-XXXXXX")));
    QVERIFY(scratch.isValid());
    const QString recording = scratch.filePath(QStringLiteral("recording.mp4"));
    const quint16 port = unusedUdpPort();
    QVERIFY(port);
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    session.start(QStringLiteral("udp://127.0.0.1:%1").arg(port));
    Pipeline sender;
    QVERIFY2(sender.start(udpSender(port, QStringLiteral("udp"))), qPrintable(sender.error()));
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= 3, 8000);
    QSignalSpy recordingStarted(session.receiver.get(), &VideoReceiver::recordingStarted);
    QSignalSpy recordingStopped(session.receiver.get(), &VideoReceiver::recordingChanged);
    QSignalSpy startComplete(session.receiver.get(), &VideoReceiver::onStartRecordingComplete);
    session.receiver->startRecording(recording, VideoReceiver::FILE_FORMAT_MP4);
    QTRY_COMPARE_WITH_TIMEOUT(startComplete.count(), 1, 5000);
    QCOMPARE(startComplete.first().first().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QTRY_COMPARE_WITH_TIMEOUT(recordingStarted.count(), 1, 5000);
    const int framesAtRecordingStart = session.frames.load();
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= framesAtRecordingStart + 6, 5000);
    session.receiver->stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!recordingStopped.isEmpty() && !recordingStopped.last().first().toBool(), 5000);
    QVERIFY(QFileInfo(recording).size() > 0);
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 1, 5000);
    QVERIFY2(sender.error().isEmpty(), qPrintable(sender.error()));
    sender.stop();

    const int previousFrames = session.frames.load();
    session.start(QStringLiteral("udp://127.0.0.1:%1").arg(port));
    const QString replay = QStringLiteral("filesrc location=\"%1\" ! qtdemux ! h264parse ! rtph264pay pt=96 config-interval=1 ! "
        "udpsink host=127.0.0.1 port=%2 sync=true async=false").arg(recording).arg(port);
    QVERIFY2(sender.start(replay), qPrintable(sender.error()));
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= previousFrames + 3, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
    QSignalSpy screenshot(session.receiver.get(), &VideoReceiver::onTakeScreenshotComplete);
    session.receiver->takeScreenshot(scratch.filePath(QStringLiteral("receiver.png")));
    QTRY_COMPARE_WITH_TIMEOUT(screenshot.count(), 1, 2000);
    QCOMPARE(screenshot.first().first().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_NOT_IMPLEMENTED);
    QVERIFY2(sender.error().isEmpty(), qPrintable(sender.error()));
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 2, 5000);
}

void GStreamerVideoReceiverTest::stopRestartAndReconnect()
{
    const quint16 port = unusedUdpPort();
    QVERIFY(port);
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    const QString uri = QStringLiteral("udp://127.0.0.1:%1").arg(port);
    Pipeline sender;
    for (int iteration = 0; iteration < 3; ++iteration) {
        session.start(uri);
        QVERIFY2(sender.start(udpSender(port, QStringLiteral("udp"))), qPrintable(sender.error()));
        const int before = session.frames.load();
        QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() > before + 2, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
        session.receiver->stop();
        QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), iteration + 1, 5000);
        QCOMPARE(session.stopped.last().first().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
        sender.stop();
    }
    session.start(uri);
    QVERIFY2(sender.start(udpSender(port, QStringLiteral("udp"))), qPrintable(sender.error()));
    const int beforeDisconnect = session.frames.load();
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() > beforeDisconnect + 2, 8000);
    sender.stop();
    QSignalSpy timedOut(session.receiver.get(), &VideoReceiver::timeout);
    QTRY_VERIFY_WITH_TIMEOUT(!timedOut.isEmpty(), 8000);
    // The production watchdog stops the receiver after emitting timeout().
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 4, 5000);
    const int beforeReconnect = session.frames.load();
    session.start(uri);
    QVERIFY2(sender.start(udpSender(port, QStringLiteral("udp"))), qPrintable(sender.error()));
    sender.setPattern(6);
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() > beforeReconnect + 2, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::blue), 3000);
    QVERIFY2(sender.error().isEmpty(), qPrintable(sender.error()));
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 5, 5000);
}

void GStreamerVideoReceiverTest::lowLatencyColorChange()
{
    const quint16 port = unusedUdpPort();
    QVERIFY(port);
    VideoSession session;
    QVERIFY2(session.open(), qPrintable(session.error));
    session.start(QStringLiteral("udp://127.0.0.1:%1").arg(port), true);
    Pipeline sender;
    QVERIFY2(sender.start(udpSender(port, QStringLiteral("udp"))), qPrintable(sender.error()));
    QTRY_VERIFY_WITH_TIMEOUT(session.frames.load() >= 5, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::red), 3000);
    QElapsedTimer elapsed;
    elapsed.start();
    sender.setPattern(6);
    QTRY_VERIFY_WITH_TIMEOUT(session.displays(Qt::blue), 1500);
    QVERIFY2(elapsed.elapsed() < 1000, qPrintable(QStringLiteral("Color-to-render latency %1ms exceeds 1000ms").arg(elapsed.elapsed())));
    QVERIFY(session.receiver->lowLatency());
    QVERIFY2(sender.error().isEmpty(), qPrintable(sender.error()));
    session.receiver->stop();
    QTRY_COMPARE_WITH_TIMEOUT(session.stopped.count(), 1, 5000);
}
