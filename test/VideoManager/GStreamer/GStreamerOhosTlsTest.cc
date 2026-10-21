#include "UnitTest.h"

#if defined(Q_OS_HARMONY) && defined(QGC_GST_STREAMING)

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>
#include <QtTest/QSignalSpy>
#include <atomic>
// gio.h declares a struct member named "signals", which Qt's keyword macro would rewrite.
#ifdef signals
#undef signals
#endif
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "GStreamer.h"
#include "GstSourceFactory.h"

namespace {

class RtspsTestServer
{
public:
    ~RtspsTestServer()
    {
        const bool hadThread = _thread != nullptr;
        if (_thread) {
            g_main_context_invoke_full(
                _context, G_PRIORITY_HIGH,
                [](gpointer data) {
                    static_cast<RtspsTestServer*>(data)->_stopOnContext();
                    return G_SOURCE_REMOVE;
                },
                this, nullptr);
            g_thread_join(_thread);
        } else {
            _stopOnContext();
        }
        if (_source) {
            g_source_unref(_source);
        }
        g_object_unref(_server);
        g_main_context_unref(_context);
        if (hadThread) {
            gst_rtsp_thread_pool_cleanup();
        }
    }

    QString start(GTlsCertificate* certificate)
    {
        gst_rtsp_server_set_address(_server, "127.0.0.1");
        gst_rtsp_server_set_service(_server, "0");
        GstRTSPAuth* auth = gst_rtsp_auth_new();
        gst_rtsp_auth_set_tls_certificate(auth, certificate);
        GstRTSPToken* token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "test", nullptr);
        gst_rtsp_auth_set_default_token(auth, token);
        gst_rtsp_token_unref(token);
        gst_rtsp_server_set_auth(_server, auth);
        g_object_unref(auth);

        GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
        gst_rtsp_media_factory_set_launch(
            factory,
            "( videotestsrc is-live=true pattern=red ! video/x-raw,format=I420,width=64,height=64,framerate=5/1 ! "
            "openh264enc ! h264parse ! rtph264pay name=pay0 pt=96 config-interval=-1 )");
        gst_rtsp_media_factory_set_protocols(factory, GST_RTSP_LOWER_TRANS_TCP);
        gst_rtsp_media_factory_set_stop_on_disconnect(factory, TRUE);
        gst_rtsp_media_factory_add_role(factory, "test", GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                        GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, nullptr);
        GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(_server);
        gst_rtsp_mount_points_add_factory(mounts, "/video", factory);
        g_object_unref(mounts);
        GstRTSPThreadPool* threads = gst_rtsp_server_get_thread_pool(_server);
        gst_rtsp_thread_pool_set_max_threads(threads, 0);
        g_object_unref(threads);

        GError* error = nullptr;
        const auto errorCleanup = qScopeGuard([&] { g_clear_error(&error); });
        _source = gst_rtsp_server_create_source(_server, nullptr, &error);
        if (!_source) {
            return error ? QString::fromUtf8(error->message) : QStringLiteral("RTSPS listener creation failed");
        }
        if (!g_source_attach(_source, _context)) {
            return QStringLiteral("RTSPS listener attach failed");
        }
        _port = gst_rtsp_server_get_bound_port(_server);
        if (_port <= 0) {
            return QStringLiteral("RTSPS listener has no bound port");
        }
        _thread = g_thread_try_new(
            "qgc-rtsps-test",
            [](gpointer data) -> gpointer {
                auto* self = static_cast<RtspsTestServer*>(data);
                g_main_context_push_thread_default(self->_context);
                while (!self->_stopping.load(std::memory_order_acquire)) {
                    g_main_context_iteration(self->_context, TRUE);
                }
                g_main_context_pop_thread_default(self->_context);
                return nullptr;
            },
            this, &error);
        return _thread ? QString() : QString::fromUtf8(error ? error->message : "RTSPS thread creation failed");
    }

    int port() const { return _port; }

private:
    void _stopOnContext()
    {
        if (_source) {
            g_source_destroy(_source);
        }
        (void) gst_rtsp_server_client_filter(
            _server, [](GstRTSPServer*, GstRTSPClient*, gpointer) { return GST_RTSP_FILTER_REMOVE; }, nullptr);
        GstRTSPSessionPool* sessions = gst_rtsp_server_get_session_pool(_server);
        (void) gst_rtsp_session_pool_filter(
            sessions, [](GstRTSPSessionPool*, GstRTSPSession*, gpointer) { return GST_RTSP_FILTER_REMOVE; }, nullptr);
        g_object_unref(sessions);
        _stopping.store(true, std::memory_order_release);
        g_main_context_wakeup(_context);
    }

    GMainContext* _context = g_main_context_new();
    GstRTSPServer* _server = gst_rtsp_server_new();
    GSource* _source = nullptr;
    GThread* _thread = nullptr;
    std::atomic<bool> _stopping{false};
    int _port = 0;
};

GTlsCertificate* loadTestCertificate(const QString& certificatePath, GError** error)
{
    QFile certificateResource(certificatePath);
    QFile keyResource(QStringLiteral(":/GStreamerTests/tls/server-public-test-key.data"));
    if (!certificateResource.open(QIODevice::ReadOnly) || !keyResource.open(QIODevice::ReadOnly)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "test certificate resource unavailable");
        return nullptr;
    }
    const QByteArray pem = certificateResource.readAll() + keyResource.readAll();
    return g_tls_certificate_new_from_pem(pem.constData(), pem.size(), error);
}

GstElement* findElementByFactory(GstElement* bin, const char* factoryName)
{
    GstIterator* iterator = gst_bin_iterate_recurse(GST_BIN(bin));
    if (!iterator) {
        return nullptr;
    }
    GstElement* found = nullptr;
    GValue item = G_VALUE_INIT;
    bool done = false;
    while (!done) {
        switch (gst_iterator_next(iterator, &item)) {
            case GST_ITERATOR_OK: {
                auto* element = GST_ELEMENT(g_value_get_object(&item));
                GstElementFactory* factory = gst_element_get_factory(element);
                const gchar* name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : nullptr;
                if (name && g_strcmp0(name, factoryName) == 0) {
                    found = GST_ELEMENT(gst_object_ref(element));
                    done = true;
                }
                g_value_reset(&item);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iterator);
                break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
                done = true;
                break;
        }
    }
    g_value_unset(&item);
    gst_iterator_free(iterator);
    return found;
}

void linkToElementSinkPad(GstElement*, GstPad* pad, gpointer data)
{
    auto* target = static_cast<GstElement*>(data);
    GstPad* sinkPad = gst_element_get_static_pad(target, "sink");
    if (!sinkPad) {
        return;
    }
    if (!gst_pad_is_linked(sinkPad)) {
        (void) gst_pad_link(pad, sinkPad);
    }
    gst_object_unref(sinkPad);
}

void linkVideoPadToElement(GstElement*, GstPad* pad, gpointer data)
{
    bool isVideo = false;
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (caps) {
        const GstStructure* structure = gst_caps_get_structure(caps, 0);
        const gchar* name = structure ? gst_structure_get_name(structure) : nullptr;
        isVideo = name && g_str_has_prefix(name, "video/");
        gst_caps_unref(caps);
    }
    if (!isVideo) {
        const gchar* padName = GST_PAD_NAME(pad);
        isVideo = padName && g_strstr_len(padName, -1, "video") != nullptr;
    }
    if (!isVideo) {
        return;
    }
    linkToElementSinkPad(nullptr, pad, data);
}

}  // namespace

class GStreamerOhosTlsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testRtspsCertificateTrust_data();
    void _testRtspsCertificateTrust();
    void _testProductionFactoryRejectsUntrustedCa_data();
    void _testProductionFactoryRejectsUntrustedCa();
};

void GStreamerOhosTlsTest::_testRtspsCertificateTrust_data()
{
    QTest::addColumn<bool>("trusted");
    QTest::newRow("ca-signed-video") << true;
    QTest::newRow("untrusted-self-signed") << false;
}

void GStreamerOhosTlsTest::_testRtspsCertificateTrust()
{
    QFETCH(bool, trusted);
    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY2(GStreamer::initialize({}, environment), "OHOS GStreamer initialization failed");
    GTlsBackend* backend = g_tls_backend_get_default();
    QVERIFY(backend);
    QCOMPARE(QByteArray(G_OBJECT_TYPE_NAME(backend)), QByteArray("GTlsBackendOpenssl"));

    if (!trusted) {
        // The rejected handshake must also produce a pipeline error and UNKNOWN_CA below.
        const QRegularExpression rejection(QStringLiteral(
            "TLS|certificate|Could not (connect to server|open resource)|can't get sdp|Failed to connect"));
        for (const char* category : {"Video.GStreamer.GStreamerAPI", "Video.GStreamer.GStreamerLogging"}) {
            ignoreLogMessage(category, QtWarningMsg, rejection);
            ignoreLogMessage(category, QtCriticalMsg, rejection);
        }
    }

    QFile caResource(QStringLiteral(":/GStreamerTests/tls/ca-certificate.data"));
    QVERIFY(caResource.open(QIODevice::ReadOnly));
    QTemporaryDir temporary(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                .filePath(QStringLiteral("qgc-rtsps-tls-XXXXXX")));
    QVERIFY(temporary.isValid());
    const QString caPath = temporary.filePath(QStringLiteral("ca.pem"));
    QFile caFile(caPath);
    QVERIFY(caFile.open(QIODevice::WriteOnly));
    const QByteArray ca = caResource.readAll();
    QCOMPARE(caFile.write(ca), ca.size());
    caFile.close();
    GError* error = nullptr;
    const auto errorCleanup = qScopeGuard([&] { g_clear_error(&error); });
    GTlsDatabase* database = g_tls_file_database_new(caPath.toUtf8().constData(), &error);
    QVERIFY2(database, error ? error->message : "Test CA database creation failed");
    const auto databaseCleanup = qScopeGuard([&] { g_object_unref(database); });
    GTlsCertificate* certificate =
        loadTestCertificate(trusted ? QStringLiteral(":/GStreamerTests/tls/server-certificate.data")
                                    : QStringLiteral(":/GStreamerTests/tls/self-signed-certificate.data"),
                            &error);
    QVERIFY2(certificate, error ? error->message : "Test TLS certificate creation failed");
    const auto certificateCleanup = qScopeGuard([&] { g_object_unref(certificate); });
    RtspsTestServer server;
    const QString serverError = server.start(certificate);
    QVERIFY2(serverError.isEmpty(), qPrintable(serverError));

    QVideoSink videoSink;
    QObject owner;
    QSignalSpy frames(&videoSink, &QVideoSink::videoFrameChanged);
    std::atomic<guint> certificateErrors{0};
    const QByteArray launch =
        QStringLiteral(
            "rtspsrc name=source location=rtsps://127.0.0.1:%1/video protocols=tcp latency=0 tcp-timeout=5000000 ! "
            "application/x-rtp,media=video,encoding-name=H264 ! rtph264depay ! h264parse ! avdec_h264 ! "
            "videoconvert ! video/x-raw,format=RGBA ! qgcvideosinkbin name=sink gpu-zerocopy=false")
            .arg(server.port())
            .toUtf8();
    GstElement* pipeline = gst_parse_launch(launch.constData(), &error);
    const auto pipelineCleanup = qScopeGuard([&] {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
    });
    QVERIFY2(!error, error ? error->message : "");
    QVERIFY(pipeline);
    GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    QVERIFY(source);
    const auto sourceCleanup = qScopeGuard([&] { gst_object_unref(source); });
    g_object_set(source, "tls-database", database, "tls-validation-flags", G_TLS_CERTIFICATE_VALIDATE_ALL, nullptr);
    g_signal_connect(source, "accept-certificate",
                     G_CALLBACK(+[](GstElement*, GTlsConnection*, GTlsCertificate*, GTlsCertificateFlags errors,
                                    gpointer data) -> gboolean {
                         static_cast<std::atomic<guint>*>(data)->fetch_or(static_cast<guint>(errors),
                                                                          std::memory_order_release);
                         return FALSE;
                     }),
                     &certificateErrors);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    QVERIFY(sink);
    const auto sinkCleanup = qScopeGuard([&] { gst_object_unref(sink); });
    QVERIFY(GStreamer::setupQVideoSinkElement(sink, &videoSink, &owner));
    GstBus* bus = gst_element_get_bus(pipeline);
    QVERIFY(bus);
    const auto busCleanup = qScopeGuard([&] { gst_object_unref(bus); });
    GstMessage* failure = nullptr;
    const auto failureCleanup = qScopeGuard([&] {
        if (failure) {
            gst_message_unref(failure);
        }
    });
    QVERIFY(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    const auto settled = [&] {
        if (!failure) {
            failure = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
        }
        return failure || !frames.isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(settled(), TestTimeout::longMs());
    if (trusted) {
        if (failure) {
            gchar* debug = nullptr;
            gst_message_parse_error(failure, &error, &debug);
            const QString diagnostic = QStringLiteral("%1 (%2)").arg(
                error ? QString::fromUtf8(error->message) : QString(), debug ? QString::fromUtf8(debug) : QString());
            g_free(debug);
            QFAIL(qPrintable(diagnostic));
        }
        QCOMPARE(certificateErrors.load(std::memory_order_acquire), 0U);
        const QImage image = videoSink.videoFrame().toImage();
        QCOMPARE(image.size(), QSize(64, 64));
        const QColor red = image.pixelColor(32, 32);
        QVERIFY(red.red() >= 240 && red.green() <= 15 && red.blue() <= 15);
    } else {
        QVERIFY2(failure, "Untrusted RTSPS server did not produce a pipeline error");
        QCOMPARE(frames.size(), 0);
        const guint errors = certificateErrors.load(std::memory_order_acquire);
        QVERIFY2(errors & G_TLS_CERTIFICATE_UNKNOWN_CA, "RTSPS rejection did not report an untrusted certificate");
        QVERIFY(
            !(errors & (G_TLS_CERTIFICATE_BAD_IDENTITY | G_TLS_CERTIFICATE_EXPIRED | G_TLS_CERTIFICATE_NOT_ACTIVATED)));
    }
}

void GStreamerOhosTlsTest::_testProductionFactoryRejectsUntrustedCa_data()
{
    QTest::addColumn<bool>("trusted");
    QTest::newRow("factory-embedded-ca-rejects-self-signed") << false;
    QTest::newRow("factory-injected-test-ca-delivers-video") << true;
}

void GStreamerOhosTlsTest::_testProductionFactoryRejectsUntrustedCa()
{
    QFETCH(bool, trusted);

    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY2(GStreamer::initialize({}, environment), "OHOS GStreamer initialization failed");

    if (!trusted) {
        // Only the exact TLS rejection wording is expected; nothing else is muted here.
        const QRegularExpression rejection(QStringLiteral(
            "TLS|certificate|Could not (connect to server|open resource)|can't get sdp|Failed to connect"));
        for (const char* category : {"Video.GStreamer.GStreamerAPI", "Video.GStreamer.GStreamerLogging"}) {
            ignoreLogMessage(category, QtWarningMsg, rejection);
            ignoreLogMessage(category, QtCriticalMsg, rejection);
        }
    }

    GError* error = nullptr;
    const auto errorCleanup = qScopeGuard([&] { g_clear_error(&error); });

    // Positive control injects a local test CA into the factory-built rtspsrc. The negative case
    // leaves the factory defaults untouched, i.e. the embedded CA bundle installed by the
    // environment, so rejecting the self-signed certificate exercises the production trust path.
    QTemporaryDir temporary(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                .filePath(QStringLiteral("qgc-rtsps-factory-XXXXXX")));
    GTlsDatabase* testDatabase = nullptr;
    const auto testDatabaseCleanup = qScopeGuard([&] {
        if (testDatabase) {
            g_object_unref(testDatabase);
        }
    });
    if (trusted) {
        QVERIFY2(temporary.isValid(), "Test CA temporary directory is invalid");
        QFile caResource(QStringLiteral(":/GStreamerTests/tls/ca-certificate.data"));
        QVERIFY(caResource.open(QIODevice::ReadOnly));
        const QString caPath = temporary.filePath(QStringLiteral("ca.pem"));
        QFile caFile(caPath);
        QVERIFY(caFile.open(QIODevice::WriteOnly));
        const QByteArray ca = caResource.readAll();
        QCOMPARE(caFile.write(ca), ca.size());
        caFile.close();
        testDatabase = g_tls_file_database_new(caPath.toUtf8().constData(), &error);
        QVERIFY2(testDatabase, error ? error->message : "Test CA database creation failed");
    }

    GTlsCertificate* certificate =
        loadTestCertificate(trusted ? QStringLiteral(":/GStreamerTests/tls/server-certificate.data")
                                    : QStringLiteral(":/GStreamerTests/tls/self-signed-certificate.data"),
                            &error);
    QVERIFY2(certificate, error ? error->message : "Test TLS certificate creation failed");
    const auto certificateCleanup = qScopeGuard([&] { g_object_unref(certificate); });
    RtspsTestServer server;
    const QString serverError = server.start(certificate);
    QVERIFY2(serverError.isEmpty(), qPrintable(serverError));

    GstElement* sourceBin =
        GStreamer::SourceFactory::create(QStringLiteral("rtsps://127.0.0.1:%1/video").arg(server.port()), {});
    QVERIFY2(sourceBin, "Production source factory could not build an RTSPS source bin");
    auto sourceBinCleanup = qScopeGuard([&] { gst_object_unref(sourceBin); });

    GstElement* rtspSource = findElementByFactory(sourceBin, "rtspsrc");
    QVERIFY2(rtspSource, "Production source bin has no rtspsrc");
    const auto rtspSourceCleanup = qScopeGuard([&] { gst_object_unref(rtspSource); });
    if (trusted) {
        g_object_set(rtspSource, "tls-database", testDatabase, "tls-validation-flags", G_TLS_CERTIFICATE_VALIDATE_ALL,
                     nullptr);
    } else {
        GTlsDatabase* factoryDatabase = nullptr;
        g_object_get(rtspSource, "tls-database", &factoryDatabase, nullptr);
        const auto factoryDatabaseCleanup = qScopeGuard([&] {
            if (factoryDatabase) {
                g_object_unref(factoryDatabase);
            }
        });
        QVERIFY2(!factoryDatabase,
                 "Production rtspsrc carried a custom TLS database instead of the embedded CA bundle");
    }
    std::atomic<guint> certificateErrors{0};
    g_signal_connect(rtspSource, "accept-certificate",
                     G_CALLBACK(+[](GstElement*, GTlsConnection*, GTlsCertificate*, GTlsCertificateFlags errors,
                                    gpointer data) -> gboolean {
                         static_cast<std::atomic<guint>*>(data)->fetch_or(static_cast<guint>(errors),
                                                                          std::memory_order_release);
                         return FALSE;
                     }),
                     &certificateErrors);

    // Real production decode chain: source bin -> decodebin3 -> convert -> RGBA -> QGC sink.
    GstElement* decoder = gst_element_factory_make("decodebin3", "decoder");
    QVERIFY2(decoder, "gst_element_factory_make('decodebin3') failed");
    auto decoderCleanup = qScopeGuard([&] { gst_object_unref(decoder); });
    GstElement* convert = gst_element_factory_make("videoconvert", "convert");
    QVERIFY2(convert, "gst_element_factory_make('videoconvert') failed");
    auto convertCleanup = qScopeGuard([&] { gst_object_unref(convert); });
    GstElement* capsFilter = gst_element_factory_make("capsfilter", "capsfilter");
    QVERIFY2(capsFilter, "gst_element_factory_make('capsfilter') failed");
    auto capsFilterCleanup = qScopeGuard([&] { gst_object_unref(capsFilter); });
    GstCaps* sinkCaps = gst_caps_from_string("video/x-raw,format=RGBA");
    g_object_set(capsFilter, "caps", sinkCaps, nullptr);
    gst_caps_unref(sinkCaps);
    GstElement* sink = gst_element_factory_make("qgcvideosinkbin", "sink");
    QVERIFY(sink);
    auto sinkCleanup = qScopeGuard([&] { gst_object_unref(sink); });
    QVideoSink videoSink;
    QObject owner;
    QSignalSpy frames(&videoSink, &QVideoSink::videoFrameChanged);
    QVERIFY(GStreamer::setupQVideoSinkElement(sink, &videoSink, &owner));

    GstElement* pipeline = gst_pipeline_new("production-rtsps");
    QVERIFY(pipeline);
    const auto pipelineCleanup = qScopeGuard([&] {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    });
    QVERIFY(gst_bin_add(GST_BIN(pipeline), sourceBin));
    sourceBinCleanup.dismiss();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), decoder));
    decoderCleanup.dismiss();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), convert));
    convertCleanup.dismiss();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), capsFilter));
    capsFilterCleanup.dismiss();
    QVERIFY(gst_bin_add(GST_BIN(pipeline), sink));
    sinkCleanup.dismiss();
    QVERIFY(gst_element_link(convert, capsFilter));
    QVERIFY(gst_element_link(capsFilter, sink));
    g_signal_connect(sourceBin, "pad-added", G_CALLBACK(linkToElementSinkPad), decoder);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(linkVideoPadToElement), convert);

    GstBus* bus = gst_element_get_bus(pipeline);
    QVERIFY(bus);
    const auto busCleanup = qScopeGuard([&] { gst_object_unref(bus); });
    QVERIFY(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    GstMessage* failure = nullptr;
    const auto failureCleanup = qScopeGuard([&] {
        if (failure) {
            gst_message_unref(failure);
        }
    });
    // The bus is polled only while no message is held: QtTest re-evaluates the condition, so a
    // pop inside the macro would consume and overwrite the first error message.
    const auto settled = [&] {
        if (!failure) {
            failure = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
        }
        return failure || !frames.isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(settled(), TestTimeout::longMs());

    if (trusted) {
        if (failure) {
            gchar* debug = nullptr;
            gst_message_parse_error(failure, &error, &debug);
            const QString diagnostic = QStringLiteral("%1 (%2)").arg(
                error ? QString::fromUtf8(error->message) : QString(), debug ? QString::fromUtf8(debug) : QString());
            g_free(debug);
            QFAIL(qPrintable(diagnostic));
        }
        QCOMPARE(certificateErrors.load(std::memory_order_acquire), 0U);
        const QImage image = videoSink.videoFrame().toImage();
        QCOMPARE(image.size(), QSize(64, 64));
        const QColor red = image.pixelColor(32, 32);
        QVERIFY(red.red() >= 240 && red.green() <= 15 && red.blue() <= 15);
    } else {
        QVERIFY2(failure, "Production GStreamer path accepted an untrusted local CA");
        QCOMPARE(frames.size(), 0);
        const guint errors = certificateErrors.load(std::memory_order_acquire);
        QVERIFY2(errors & G_TLS_CERTIFICATE_UNKNOWN_CA,
                 "Production factory path did not report an untrusted certificate");
        QVERIFY(
            !(errors & (G_TLS_CERTIFICATE_BAD_IDENTITY | G_TLS_CERTIFICATE_EXPIRED | G_TLS_CERTIFICATE_NOT_ACTIVATED)));
    }
}

UT_REGISTER_TEST(GStreamerOhosTlsTest, TestLabel::Integration)

#include "GStreamerOhosTlsTest.moc"

#endif
