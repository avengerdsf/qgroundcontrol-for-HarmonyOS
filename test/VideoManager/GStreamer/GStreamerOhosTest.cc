#include "UnitTest.h"

#if defined(Q_OS_HARMONY) && defined(QGC_GST_STREAMING)

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QScopeGuard>
#include <QtCore/QSemaphore>
#include <QtCore/QStandardPaths>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QVideoSink>
#include <QtTest/QSignalSpy>
#include <chrono>
#include <cstdlib>
// gio.h declares a struct member named "signals", which Qt's keyword macro would rewrite.
#ifdef signals
#undef signals
#endif
#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <memory>
#include <thread>
#include <vector>

#include "Fixtures/RAIIFixtures.h"
#include "GStreamer.h"
#include "GStreamerEnvironment.h"
#include "GstTestContextWiring.h"

#if defined(QGC_HAS_GST_GLMEMORY_GPU_PATH)
#include <EGL/egl.h>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <gst/gl/gl.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include "GstGlContextBridge.h"
#include "GstGlVideoBuffer.h"
#endif

namespace {
struct BusClockStep
{
    gint64 delta = 0;
    gint64 initial = 0;
    int calls = 0;
    QSemaphore firstSample;
    QSemaphore secondSample;
};

thread_local BusClockStep* busClockStep = nullptr;
}  // namespace

// Only this test target links with --wrap. Other threads and production builds use the real clock.
extern "C" gint64 __real_g_get_monotonic_time();

extern "C" gint64 __wrap_g_get_monotonic_time()
{
    if (busClockStep) {
        if (++busClockStep->calls == 1) {
            busClockStep->initial = __real_g_get_monotonic_time();
            busClockStep->firstSample.release();
            return busClockStep->initial;
        }
        if (busClockStep->calls == 2) {
            busClockStep->secondSample.release();
            return busClockStep->initial + busClockStep->delta;
        }
    }
    return __real_g_get_monotonic_time();
}

class GStreamerOhosTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testSandboxEnvironment();
    void _testStaticFactories();
    void _testBusWaitTimeout();
    void _testBusWaitForMessage();
    void _testBusClockStep_data();
    void _testBusClockStep();
    void _testSoftwareDecodeFrames_data();
    void _testSoftwareDecodeFrames();
    void _testGlTextureImport_data();
    void _testGlTextureImport();
};

void GStreamerOhosTest::_testBusClockStep_data()
{
    QTest::addColumn<qint64>("delta");
    QTest::newRow("backwards") << qint64(-31);
    QTest::newRow("equal") << qint64(0);
    QTest::newRow("forward") << qint64(31);
}

void GStreamerOhosTest::_testBusClockStep()
{
    QFETCH(qint64, delta);
    GstBus* bus = gst_bus_new();
    QVERIFY(bus);
    const auto cleanup = qScopeGuard([&] { gst_object_unref(bus); });
    BusClockStep step;
    step.delta = delta;
    const gboolean debugWasActive = gst_debug_is_active();
    gst_debug_set_active(FALSE);
    const auto debugCleanup = qScopeGuard([&] { gst_debug_set_active(debugWasActive); });
    std::thread producer([&] {
        if (step.firstSample.tryAcquire(1, 5000)) {
            gst_bus_post(bus, gst_message_new_application(nullptr, gst_structure_new_empty("wake")));
            if (step.secondSample.tryAcquire(1, 5000)) {
                gst_bus_post(bus, gst_message_new_eos(nullptr));
            }
        }
    });
    const auto producerCleanup = qScopeGuard([&] { producer.join(); });
    busClockStep = &step;
    const auto clockCleanup = qScopeGuard([] { busClockStep = nullptr; });
    GstMessage* message = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_EOS);
    const auto messageCleanup = qScopeGuard([&] {
        if (message) {
            gst_message_unref(message);
        }
    });
    QVERIFY2(message, "Clock step incorrectly expired the bus before EOS");
    QCOMPARE(GST_MESSAGE_TYPE(message), GST_MESSAGE_EOS);
    QVERIFY2(step.calls >= 2, "The actual bus wait did not observe the injected clock step");
}

void GStreamerOhosTest::_testBusWaitTimeout()
{
    GstBus* bus = gst_bus_new();
    QVERIFY(bus);
    const auto cleanup = qScopeGuard([&] { gst_object_unref(bus); });
    // An empty bus must wait for the requested deadline, including after other pipelines have
    // exercised the poll control descriptor. Returning early can turn real EOS into false timeouts.
    for (int iteration = 0; iteration < 20; ++iteration) {
        QElapsedTimer elapsed;
        elapsed.start();
        GstMessage* message = gst_bus_timed_pop_filtered(bus, 50 * GST_MSECOND, GST_MESSAGE_APPLICATION);
        if (message) {
            gst_message_unref(message);
            QFAIL("An empty bus returned a message");
        }
        QVERIFY2(
            elapsed.elapsed() >= 40,
            qPrintable(
                QStringLiteral("Bus wait returned after %1 ms at iteration %2").arg(elapsed.elapsed()).arg(iteration)));
    }
}

void GStreamerOhosTest::_testBusWaitForMessage()
{
    GstBus* bus = gst_bus_new();
    QVERIFY(bus);
    const auto cleanup = qScopeGuard([&] { gst_object_unref(bus); });
    std::thread producer([&] {
        for (int sequence = 0; sequence < 25; ++sequence) {
            // Deliberately post after the consumer enters its timed wait.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            gst_bus_post(bus, gst_message_new_application(
                                  nullptr, gst_structure_new("wake", "sequence", G_TYPE_INT, sequence, nullptr)));
        }
    });
    const auto producerCleanup = qScopeGuard([&] { producer.join(); });
    for (int sequence = 0; sequence < 25; ++sequence) {
        GstMessage* message = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_APPLICATION);
        const auto messageCleanup = qScopeGuard([&] {
            if (message) {
                gst_message_unref(message);
            }
        });
        QVERIFY2(message, "Bus wait expired before the scheduled message");
        int actual = -1;
        QVERIFY(gst_structure_get_int(gst_message_get_structure(message), "sequence", &actual));
        QCOMPARE(actual, sequence);
    }
}

void GStreamerOhosTest::_testSoftwareDecodeFrames_data()
{
    QTest::addColumn<QString>("codec");
    QTest::newRow("h264") << QStringLiteral("openh264enc ! h264parse ! avdec_h264");
    QTest::newRow("vp8") << QStringLiteral("vp8enc deadline=1 ! vp8dec");
    QTest::newRow("vp9") << QStringLiteral("vp9enc deadline=1 ! vp9dec");
}

void GStreamerOhosTest::_testSoftwareDecodeFrames()
{
    QFETCH(QString, codec);
    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY2(GStreamer::initialize({}, environment), "OHOS GStreamer initialization failed");
    QVideoSink videoSink;
    QObject owner;
    QSignalSpy frames(&videoSink, &QVideoSink::videoFrameChanged);
    const QByteArray launch =
        QStringLiteral(
            "videotestsrc pattern=white num-buffers=3 ! video/x-raw,format=I420,width=64,height=64,framerate=5/1 ! "
            "%1 ! videoconvert ! video/x-raw,format=RGBA ! qgcvideosinkbin name=sink gpu-zerocopy=false")
            .arg(codec)
            .toUtf8();
    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(launch.constData(), &error);
    const auto cleanup = qScopeGuard([&] {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        g_clear_error(&error);
    });
    QVERIFY2(!error, error ? error->message : "");
    QVERIFY(pipeline);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    QVERIFY(sink);
    const auto sinkCleanup = qScopeGuard([&] { gst_object_unref(sink); });
    QVERIFY(GStreamer::setupQVideoSinkElement(sink, &videoSink, &owner));
    QVERIFY(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), TestTimeout::longMs());
    const QImage image = videoSink.videoFrame().toImage();
    QCOMPARE(image.size(), QSize(64, 64));
    const QColor pixel = image.pixelColor(32, 32);
    QVERIFY(pixel.red() >= 245 && pixel.green() >= 245 && pixel.blue() >= 245);
    QCOMPARE(pixel.alpha(), 255);
}

void GStreamerOhosTest::_testSandboxEnvironment()
{
    std::vector<TestFixtures::EnvVarFixture> backups;
    for (const char* name : {"GST_PLUGIN_PATH",
                             "GST_PLUGIN_PATH_1_0",
                             "GST_PLUGIN_SYSTEM_PATH",
                             "GST_PLUGIN_SYSTEM_PATH_1_0",
                             "GST_PLUGIN_SCANNER",
                             "GST_PLUGIN_SCANNER_1_0",
                             "GST_REGISTRY",
                             "GST_REGISTRY_1_0",
                             "GST_REGISTRY_FORK",
                             "GST_REGISTRY_REUSE_PLUGIN_SCANNER",
                             "CA_CERTIFICATES",
                             "SSL_CERT_FILE",
                             "HOME",
                             "TMP",
                             "TEMP",
                             "TMPDIR",
                             "XDG_DATA_HOME",
                             "XDG_CONFIG_HOME",
                             "XDG_CACHE_HOME",
                             "XDG_RUNTIME_DIR",
                             "GIO_MODULE_DIR",
                             "GIO_EXTRA_MODULES",
                             "GIO_USE_VFS",
                             "GST_GL_PLATFORM",
                             "GST_GL_API",
                             "GST_GL_WINDOW"}) {
        backups.emplace_back(name);
        if (QByteArray(name).startsWith("GST_") || QByteArray(name) == "CA_CERTIFICATES" ||
            QByteArray(name) == "SSL_CERT_FILE") {
            qputenv(name, "/unavailable/desktop/gstreamer");
        }
    }
    const auto result = GStreamer::Environment::prepareEnvironment();
    QVERIFY2(result.ok, qPrintable(result.error));
    for (const char* name :
         {"GST_PLUGIN_PATH", "GST_PLUGIN_PATH_1_0", "GST_PLUGIN_SYSTEM_PATH", "GST_PLUGIN_SYSTEM_PATH_1_0"}) {
        QVERIFY(qEnvironmentVariableIsSet(name));
        QVERIFY(qEnvironmentVariableIsEmpty(name));
    }
    QVERIFY(!qEnvironmentVariableIsSet("GST_PLUGIN_SCANNER"));
    QVERIFY(!qEnvironmentVariableIsSet("GST_PLUGIN_SCANNER_1_0"));
    QCOMPARE(qgetenv("GST_REGISTRY_FORK"), QByteArray("no"));
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QFileInfo registry(qEnvironmentVariable("GST_REGISTRY_1_0"));
    QCOMPARE(registry.absolutePath(), QDir(cache).absolutePath());
    QVERIFY(QFileInfo(registry.absolutePath()).isWritable());
    QCOMPARE(qgetenv("GST_REGISTRY"), qgetenv("GST_REGISTRY_1_0"));
    QVERIFY(QFileInfo(qEnvironmentVariable("CA_CERTIFICATES")).isReadable());
#if !defined(QGC_GST_OHOS_CPU_ONLY)
    QCOMPARE(qgetenv("GST_GL_PLATFORM"), QByteArray("egl"));
    QCOMPARE(qgetenv("GST_GL_API"), QByteArray("gles2"));
    QCOMPARE(qgetenv("GST_GL_WINDOW"), QByteArray("surfaceless"));
#endif
}

void GStreamerOhosTest::_testGlTextureImport_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QColor>("expected");
    QTest::addColumn<QString>("format");
    QTest::newRow("red") << QStringLiteral("red") << QColor(255, 0, 0, 255) << QStringLiteral("RGBA");
    QTest::newRow("blue") << QStringLiteral("blue") << QColor(0, 0, 255, 255) << QStringLiteral("RGBA");
    QTest::newRow("red-16bit") << QStringLiteral("red") << QColor(255, 0, 0, 255) << QStringLiteral("RGBA64_LE");
    QTest::newRow("blue-16bit") << QStringLiteral("blue") << QColor(0, 0, 255, 255) << QStringLiteral("RGBA64_LE");
}

void GStreamerOhosTest::_testGlTextureImport()
{
#if defined(QGC_GST_OHOS_CPU_ONLY)
    QSKIP("GLMemory is disabled by the explicit HarmonyOS CPU-only build profile");
#elif defined(QGC_HAS_GST_GLMEMORY_GPU_PATH)
    QFETCH(QString, pattern);
    QFETCH(QColor, expected);
    QFETCH(QString, format);
    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY2(GStreamer::initialize({}, environment), "OHOS GStreamer initialization failed");
    QOpenGLContext* shareContext = QOpenGLContext::globalShareContext();
    QVERIFY2(shareContext, "Qt global EGL share context is unavailable");
    std::unique_ptr<QOffscreenSurface> surface(QRhiGles2InitParams::newFallbackSurface(shareContext->format()));
    QVERIFY(surface && surface->isValid());
    QRhiGles2InitParams params;
    params.fallbackSurface = surface.get();
    params.shareContext = shareContext;
    params.format = shareContext->format();
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &params));
    QVERIFY2(rhi, "HarmonyOS GLES QRhi creation failed");
    GstGlContextBridge::reset();
    const auto bridgeCleanup = qScopeGuard([] { GstGlContextBridge::reset(); });
    const EGLDisplay previousDisplay = eglGetCurrentDisplay();
    const EGLContext previousContext = eglGetCurrentContext();
    const EGLSurface previousDraw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface previousRead = eglGetCurrentSurface(EGL_READ);
    QVERIFY2(GstGlContextBridge::prime(), "HarmonyOS Qt/GStreamer EGL bridge could not share a context");
    QCOMPARE(eglGetCurrentDisplay(), previousDisplay);
    QCOMPARE(eglGetCurrentContext(), previousContext);
    QCOMPARE(eglGetCurrentSurface(EGL_DRAW), previousDraw);
    QCOMPARE(eglGetCurrentSurface(EGL_READ), previousRead);
    GError* error = nullptr;
    const QByteArray launch =
        QStringLiteral(
            "videotestsrc pattern=%1 num-buffers=1 ! "
            "video/x-raw,format=%2,width=64,height=64,framerate=1/1 ! videoconvert ! glupload ! glcolorconvert ! "
            "video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D ! appsink name=frames sync=false")
            .arg(pattern, format)
            .toUtf8();
    GstElement* pipeline = gst_parse_launch(launch.constData(), &error);
    const auto pipelineCleanup = qScopeGuard([&] {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        g_clear_error(&error);
    });
    QVERIFY2(!error, error ? error->message : "");
    QVERIFY(pipeline);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "frames");
    QVERIFY(sink);
    const auto sinkCleanup = qScopeGuard([&] { gst_object_unref(sink); });
    GstBus* bus = gst_element_get_bus(pipeline);
    QVERIFY(bus);
    gst_object_unref(bus);
    // Raw pipeline: install the production context wiring so glupload receives the Qt-shared
    // GstGLDisplay/GstGLContext instead of creating its own (which fails on OHOS).
    GstTestContextWiring::installBusHandler(pipeline);
    QVERIFY2(GstTestContextWiring::installSinkQueryProbe(sink) != 0,
             "appsink sink pad could not answer downstream context queries");
    QVERIFY(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_SECOND);
    QVERIFY2(sample, "HarmonyOS glupload produced no sample");
    const auto sampleCleanup = qScopeGuard([&] { gst_sample_unref(sample); });
    GstVideoInfo info;
    gst_video_info_init(&info);
    QVERIFY(gst_video_info_from_caps(&info, gst_sample_get_caps(sample)));
    GstGlVideoBuffer videoBuffer(sample, info, QVideoFrameFormat(QSize(64, 64), QVideoFrameFormat::Format_RGBA8888));
    QVERIFY2(videoBuffer.validatePlaneHandles(), "glupload output is not real GstGLMemory");
    QVideoFrameTexturesUPtr old;
    auto textures = videoBuffer.mapTextures(*rhi, old);
    QVERIFY2(textures && textures->texture(0), "GstGLMemory could not be imported as a QRhi texture");
    QOpenGLContext* current = QOpenGLContext::currentContext();
    QVERIFY(current);
    GstMemory* memory = gst_buffer_peek_memory(gst_sample_get_buffer(sample), 0);
    QVERIFY(memory && gst_is_gl_memory(memory));
    GstGLContext* gstContext = GST_GL_MEMORY_CAST(memory)->mem.context;
    const bool hasNorm16 = current->hasExtension(QByteArrayLiteral("GL_EXT_texture_norm16"));
    QCOMPARE(bool(gst_gl_format_is_supported(gstContext, GST_GL_RGBA16)), hasNorm16);
    QOpenGLFunctions* gl = current->functions();
    const GLuint texture = static_cast<GLuint>(textures->texture(0)->nativeTexture().object);
    QVERIFY2(gl->glIsTexture(texture), "Imported QRhi texture is not visible in Qt's shared EGL context");
    GLint previousFramebuffer = 0;
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    GLuint framebuffer = 0;
    gl->glGenFramebuffers(1, &framebuffer);
    const auto framebufferCleanup = qScopeGuard([&] {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        gl->glDeleteFramebuffers(1, &framebuffer);
    });
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    QCOMPARE(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
    unsigned char pixel[4] = {};
    gl->glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    QCOMPARE(gl->glGetError(), GLenum(GL_NO_ERROR));
    const int expectedRgba[] = {expected.red(), expected.green(), expected.blue(), expected.alpha()};
    for (int channel = 0; channel < 4; ++channel) {
        QVERIFY2(std::abs(int(pixel[channel]) - expectedRgba[channel]) <= 2,
                 qPrintable(QStringLiteral("Imported texture channel %1: expected %2, got %3")
                                .arg(channel)
                                .arg(expectedRgba[channel])
                                .arg(pixel[channel])));
    }
#else
    QFAIL("HarmonyOS GL profile has no compiled GLMemory import path");
#endif
}

void GStreamerOhosTest::_testStaticFactories()
{
    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY2(GStreamer::initialize({}, environment), "OHOS GStreamer initialization failed");
    GTlsBackend* tls = g_tls_backend_get_default();
    QVERIFY2(tls && g_tls_backend_supports_tls(tls), "OHOS GIO TLS backend is unavailable");
    GTlsDatabase* database = g_tls_backend_get_default_database(tls);
    // The reference is transfer-full, so guard it before any assertion can return early.
    const auto databaseCleanup = qScopeGuard([&] {
        if (database) {
            g_object_unref(database);
        }
    });
    QVERIFY2(database, "OHOS GIO CA database was not installed");
    QVERIFY2(G_IS_TLS_FILE_DATABASE(database), "OHOS GIO CA database is not a file database");
    gchar* anchors = nullptr;
    g_object_get(database, "anchors", &anchors, nullptr);
    QCOMPARE(QString::fromUtf8(anchors ? anchors : ""), qEnvironmentVariable("CA_CERTIFICATES"));
    g_free(anchors);
    QFile caBundle(qEnvironmentVariable("CA_CERTIFICATES"));
    QVERIFY2(caBundle.open(QIODevice::ReadOnly), "OHOS CA bundle is not readable");
    const QByteArray caData = caBundle.readAll();
    QCOMPARE(QCryptographicHash::hash(caData, QCryptographicHash::Sha256).toHex(),
             QByteArray("f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"));
    for (const char* name : {"appsrc",       "appsink",      "videotestsrc", "videoconvert",  "videoscale",
                             "decodebin",    "avdec_h264",   "avdec_h265",   "openh264enc",   "vp8dec",
                             "vp9dec",       "h264parse",    "h265parse",    "rtspsrc",       "rtpjitterbuffer",
                             "rtph264depay", "rtph265depay", "udpsrc",       "tcpclientsrc",  "tsdemux",
                             "splitmuxsink", "mp4mux",       "matroskamux",  "qgcqvideosink", "qgcvideosinkbin"}) {
        GstElementFactory* factory = gst_element_factory_find(name);
        QVERIFY2(factory, name);
        gst_object_unref(factory);
    }
#if !defined(QGC_GST_OHOS_CPU_ONLY)
    for (const char* name : {"glupload", "glcolorconvert"}) {
        GstElementFactory* factory = gst_element_factory_find(name);
        QVERIFY2(factory, name);
        gst_object_unref(factory);
    }
#endif
}

UT_REGISTER_TEST(GStreamerOhosTest, TestLabel::Integration)

#include "GStreamerOhosTest.moc"

#endif
