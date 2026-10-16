#include "Fixture.h"
#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#ifdef Q_OS_HARMONY
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <cstdio>
#endif

Q_LOGGING_CATEGORY(LinkManagerLog, "test.nmea")
Q_LOGGING_CATEGORY(LinkManagerVerboseLog, "test.nmea.verbose", QtWarningMsg)

namespace {
QByteArray sentence(const QByteArray &body)
{
    quint8 checksum = 0;
    for (const char value : body) { checksum ^= quint8(value); }
    return "$" + body + '*' + QByteArray::number(checksum, 16).rightJustified(2, '0').toUpper() + "\r\n";
}
QByteArray fix()
{
    return sentence("GPRMC,120001.000,A,4807.038,N,01131.000,E,0.0,0.0,151026,,,A") +
        sentence("GPGGA,120001.000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
}
class Provider final : public HarmonySerialProvider
{
public:
    QList<Request> requests;
    QMap<quint64, Completion> reads, opens;
    bool holdOpen = false;
    QString openError;
    QList<HarmonySerialPortInfo> ports(QString *, bool *) override { return {}; }
    void call(const Request &request, Completion completion) override
    {
        requests.append(request);
        if (request.operation == Operation::Open && holdOpen) { opens.insert(request.session, std::move(completion)); }
        else if (request.operation == Operation::Read) { reads.insert(request.session, std::move(completion)); }
        else { Result result; if (request.operation == Operation::Open) { result.error = openError; } completion(result); }
    }
    int count(Operation operation) const
    {
        return int(std::count_if(requests.cbegin(), requests.cend(), [operation](const Request &r) { return r.operation == operation; }));
    }
    Request last(Operation operation) const
    {
        for (auto i = requests.crbegin(); i != requests.crend(); ++i) { if (i->operation == operation) { return *i; } }
        return {};
    }
    void deliver(quint64 session, const QByteArray &data)
    {
        auto callback = reads.take(session);
        if (callback) { Result result; result.data = data; callback(result); }
    }
};
}

class NmeaSerialSourceTest : public QObject
{
    Q_OBJECT
    std::shared_ptr<Provider> provider;
private slots:
    void init()
    {
        provider = std::make_shared<Provider>();
        HarmonySerialBridge::setTestProvider(provider);
        QGCSerialPortInfo::available = {QGCSerialPortInfo("harmony-serial:42")};
        qgcApp()->messages.clear();
    }
    void cleanup()
    {
        QGCPositionManager::instance()->reset();
        HarmonySerialBridge::setTestProvider({});
    }
    void configuredPortFeedsRealNmeaParser()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        QElapsedTimer elapsed; elapsed.start();
        manager._updateAutoConnectLinks();
        QVERIFY(elapsed.elapsed() < 100);
        QTRY_COMPARE_WITH_TIMEOUT(provider->count(Provider::Operation::Open), 1, 500);
        QCOMPARE(provider->last(Provider::Operation::Open).portName, QString("harmony-serial:42"));
        QCOMPARE(provider->last(Provider::Operation::Open).settings.baud, 4800);
        auto *source = QGCPositionManager::instance()->_nmeaSource;
        QVERIFY(source);
        QCOMPARE(source->device(), manager.nmeaDevice());
        QSignalSpy updates(source, &QNmeaPositionInfoSource::positionUpdated);
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        const quint64 session = provider->last(Provider::Operation::Open).session;
        const QByteArray data = fix();
        provider->deliver(session, data.first(7));
        QCoreApplication::processEvents();
        QVERIFY(!manager.nmeaDevice()->canReadLine());
        QCOMPARE(updates.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(provider->reads.contains(session), 500);
        provider->deliver(session, data.mid(7));
        QTRY_VERIFY_WITH_TIMEOUT(!updates.isEmpty(), 1500);
        const QGeoPositionInfo position = updates.last().first().value<QGeoPositionInfo>();
        QVERIFY(qAbs(position.coordinate().latitude() - 48.1173) < 1e-7);
        QVERIFY(qAbs(position.coordinate().longitude() - (11.0 + 31.0 / 60.0)) < 1e-7);
    }
    void baudChangesWhileAuthorizationIsPending()
    {
        provider->holdOpen = true;
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->opens.size(), 1, 500);
        manager.settings.baud.value = 38400;
        manager._updateAutoConnectLinks();
        auto callback = provider->opens.take(provider->opens.firstKey());
        callback({});
        QTRY_COMPARE_WITH_TIMEOUT(provider->count(Provider::Operation::SetBaud), 1, 500);
        QCOMPARE(provider->last(Provider::Operation::SetBaud).settings.baud, 38400);
        manager.settings.baud.value = 57600;
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->last(Provider::Operation::SetBaud).settings.baud, 57600, 500);
    }
    void disableCancelsPendingOpenAndLateGrant()
    {
        provider->holdOpen = true;
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->opens.size(), 1, 500);
        QPointer<QIODevice> old = manager.nmeaDevice();
        manager.settings.port.value = "Disabled";
        manager._updateAutoConnectLinks();
        QVERIFY(old.isNull());
        QCOMPARE(provider->count(Provider::Operation::Close), 1);
        auto callback = provider->opens.take(provider->opens.firstKey());
        callback({});
        QCoreApplication::processEvents();
        QCOMPARE(provider->count(Provider::Operation::Read), 0);
    }
    void denialIsVisibleAndOnlyExplicitReselectionRetries()
    {
        provider->openError = "USB serial access was denied (31400002)";
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(qgcApp()->messages.size(), 1, 500);
        QVERIFY(qgcApp()->messages.first().contains("denied"));
        QVERIFY(!manager.nmeaDevice()->isOpen());
        for (int i = 0; i < 3; ++i) { manager._updateAutoConnectLinks(); }
        QCOMPARE(provider->count(Provider::Operation::Open), 1);
        manager.settings.port.value = "Disabled";
        manager._updateAutoConnectLinks();
        manager.settings.port.value = "harmony-serial:42";
        provider->openError.clear();
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->count(Provider::Operation::Open), 2, 500);
    }
    void unplugAndUdpSwitchReleaseTheSerialDevice()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        QPointer<QIODevice> old = manager.nmeaDevice();
        QGCSerialPortInfo::available.clear();
        manager._updateAutoConnectLinks();
        QVERIFY(old.isNull());
        QGCSerialPortInfo::available = {QGCSerialPortInfo("harmony-serial:42")};
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->count(Provider::Operation::Open), 2, 500);
        manager.settings.port.value = "UDP Port";
        manager._updateAutoConnectLinks();
        QVERIFY(!manager.nmeaDevice());
        QCOMPARE(QGCPositionManager::instance()->_nmeaSource->device(), static_cast<QIODevice *>(manager._nmeaSocket));
        QCOMPARE(provider->count(Provider::Operation::Close), 2);
    }
    void oldReadsCannotReachAReplacementSource()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        const quint64 oldSession = provider->last(Provider::Operation::Open).session;
        QGCSerialPortInfo::available.append(QGCSerialPortInfo("harmony-serial:43"));
        manager.settings.port.value = "harmony-serial:43";
        manager._updateAutoConnectLinks();
        QTRY_COMPARE_WITH_TIMEOUT(provider->count(Provider::Operation::Open), 2, 500);
        QSignalSpy updates(QGCPositionManager::instance()->_nmeaSource, &QNmeaPositionInfoSource::positionUpdated);
        provider->deliver(oldSession, fix());
        QCoreApplication::processEvents();
        QCOMPARE(updates.count(), 0);
        QCOMPARE(manager.nmeaDevice()->bytesAvailable(), 0);
    }
    void disableStopsPendingPositionUpdates()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        auto *source = QGCPositionManager::instance()->_nmeaSource;
        source->setUpdateInterval(200);
        QSignalSpy updates(source, &QNmeaPositionInfoSource::positionUpdated);
        provider->deliver(provider->last(Provider::Operation::Open).session, fix());
        QCoreApplication::processEvents();
        QCOMPARE(updates.count(), 0);
        manager.settings.port.value = "Disabled";
        manager._updateAutoConnectLinks();
        QVERIFY(!manager.nmeaDevice());
        QTest::qWait(450);
        QCOMPARE(updates.count(), 0);
    }
    void fifoLineReadsPreserveTheIncompleteTail()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        const quint64 session = provider->last(Provider::Operation::Open).session;
        QIODevice *const device = manager.nmeaDevice();
        device->blockSignals(true);
        provider->deliver(session, "$ABC");
        QCoreApplication::processEvents();
        QVERIFY(!device->canReadLine());
        QCOMPARE(device->bytesAvailable(), 4);
        QTRY_VERIFY_WITH_TIMEOUT(provider->reads.contains(session), 500);
        provider->deliver(session, ",DEF\r\nTAIL");
        QCoreApplication::processEvents();
        QVERIFY(device->canReadLine());
        QCOMPARE(device->readLine(), QByteArray("$ABC,DEF\r\n"));
        QVERIFY(!device->canReadLine());
        QCOMPARE(device->readAll(), QByteArray("TAIL"));
        device->blockSignals(false);
    }
    void oversizedUnterminatedInputFailsWithoutDroppingBytes()
    {
        LinkManager manager;
        manager.settings.port.value = "harmony-serial:42";
        manager._updateAutoConnectLinks();
        QTRY_VERIFY_WITH_TIMEOUT(!provider->reads.isEmpty(), 500);
        const quint64 session = provider->last(Provider::Operation::Open).session;
        manager.nmeaDevice()->blockSignals(true);
        for (int i = 0; i < 8; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(provider->reads.contains(session), 500);
            provider->deliver(session, QByteArray(8192, 'x'));
            QCoreApplication::processEvents();
        }
        QCOMPARE(manager.nmeaDevice()->bytesAvailable(), 65536);
        QVERIFY(!manager.nmeaDevice()->canReadLine());
        manager.nmeaDevice()->blockSignals(false);
        QTRY_VERIFY_WITH_TIMEOUT(provider->reads.contains(session), 500);
        provider->deliver(session, QByteArray(1, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(qgcApp()->messages.size(), 1, 500);
        QVERIFY(!manager.nmeaDevice()->isOpen());
        QCOMPARE(manager.nmeaDevice()->bytesAvailable(), 0);
        QCOMPARE(provider->count(Provider::Operation::Close), 1);
    }
};
#ifdef Q_OS_HARMONY
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QWindow window;
    window.setTitle(QStringLiteral("QGC NMEA Tests"));
    window.resize(640, 360);
    window.show();
    window.requestActivate();
    const QString mode = qEnvironmentVariable("QGC_NMEA_RUN");
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer ready;
    ready.setInterval(100);
    QObject::connect(&ready, &QTimer::timeout, &app, [&]() {
        if (!window.isExposed()) {
            if (elapsed.elapsed() < 15000) { return; }
            ready.stop();
            std::fprintf(stdout, "QGC_NMEA_DONE exit=2 window-not-ready\n");
            std::fflush(stdout);
            app.exit(2);
            return;
        }
        ready.stop();
        std::fprintf(stdout, "QGC_NMEA_READY mode=%s qt=%s snapshot=%s\n",
            mode.isEmpty() ? "idle" : "controlled-provider", QT_VERSION_STR, TEST_SOURCE_SNAPSHOT);
        std::fflush(stdout);
        if (mode.isEmpty()) { return; }
        int result = 2;
        if (mode == QStringLiteral("1")) {
            NmeaSerialSourceTest test;
            result = QTest::qExec(&test, app.arguments());
        }
        std::fprintf(stdout, "QGC_NMEA_DONE exit=%d\n", result);
        std::fflush(stdout);
        app.exit(result);
    });
    ready.start();
    return app.exec();
}
#else
QTEST_GUILESS_MAIN(NmeaSerialSourceTest)
#endif
#include "NmeaSerialSourceTest.moc"
