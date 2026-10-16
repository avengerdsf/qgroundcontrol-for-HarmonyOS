#include "HarmonyBluetoothBridgeTest.h"
#include "HarmonyBluetoothBridge.h"
#ifdef QGC_ENABLE_BLUETOOTH
#include "BluetoothLink.h"
#include "LinkManager.h"
#endif

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>

namespace {
class BluetoothTestProvider final : public HarmonyBluetoothProvider
{
public:
    bool supported(QString *, bool *factoryReady) override
    {
        if (factoryReady) { *factoryReady = ready; }
        return available;
    }
    void call(const Request &request, Completion completion) override
    {
        const QMutexLocker locker(&mutex);
        requests.append(request);
        callbacks.append(std::move(completion));
    }
    bool has(Operation operation) const
    {
        const QMutexLocker locker(&mutex);
        return std::any_of(requests.cbegin(), requests.cend(), [operation](const Request &request) { return request.operation == operation; });
    }
    Request request(Operation operation) const
    {
        const QMutexLocker locker(&mutex);
        const auto found = std::find_if(requests.cbegin(), requests.cend(), [operation](const Request &request) { return request.operation == operation; });
        return found == requests.cend() ? Request{} : *found;
    }
    void finish(Operation operation, const Result &result = {})
    {
        Completion completion;
        {
            const QMutexLocker locker(&mutex);
            for (int i = 0; i < requests.size(); ++i) {
                if (requests[i].operation == operation) {
                    requests.removeAt(i);
                    completion = callbacks.takeAt(i);
                    break;
                }
            }
        }
        if (completion) { completion(result); }
    }
    bool available = true;
    bool ready = true;
    mutable QMutex mutex;
    QList<Request> requests;
    QList<Completion> callbacks;
};

class ProviderGuard
{
public:
    ProviderGuard() : provider(std::make_shared<BluetoothTestProvider>()) { HarmonyBluetoothBridge::setTestProvider(provider); }
    ~ProviderGuard() { HarmonyBluetoothBridge::setTestProvider({}); }
    std::shared_ptr<BluetoothTestProvider> provider;
};
using Operation = HarmonyBluetoothProvider::Operation;
const QString address = QStringLiteral("12:34:56:78:90:AB");
}

void HarmonyBluetoothBridgeTest::cleanup()
{
    UnitTest::cleanup();
#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
    LinkManager::instance()->disconnectAll();
    QTRY_VERIFY(LinkManager::instance()->links().isEmpty());
#endif
}

void HarmonyBluetoothBridgeTest::_authorizationIsAsynchronous()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
    bridge.open(address);
    QVERIFY(guard.provider->has(Operation::Open));
    QCOMPARE(guard.provider->request(Operation::Open).address, address);
    QVERIFY(!bridge.isOpen());
    QCOMPARE(connected.count(), 0);
    guard.provider->finish(Operation::Open);
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(bridge.isOpen());
    QVERIFY(guard.provider->has(Operation::Read));
}

void HarmonyBluetoothBridgeTest::_discoveryUsesNativeAddresses()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy found(&bridge, &HarmonyBluetoothBridge::deviceDiscovered);
    QSignalSpy changed(&bridge, &HarmonyBluetoothBridge::scanningChanged);
    bridge.startScan();
    QVERIFY(bridge.isScanning());
    QVERIFY(guard.provider->has(Operation::StartScan));
    HarmonyBluetoothProvider::Result result;
    result.devices = {{address, QStringLiteral("SPP telemetry")}};
    result.scanning = true;
    guard.provider->finish(Operation::StartScan, result);
    QTRY_COMPARE(found.count(), 1);
    QCOMPARE(found[0][0].toString(), address);
    QCOMPARE(found[0][1].toString(), QStringLiteral("SPP telemetry"));
    QTRY_VERIFY(guard.provider->has(Operation::Scan));
    result.scanning = false;
    result.devices.clear();
    guard.provider->finish(Operation::Scan, result);
    QTRY_VERIFY(!bridge.isScanning());
    QCOMPARE(changed.count(), 2);
}

void HarmonyBluetoothBridgeTest::_stopScanDiscardsLatePermission()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy found(&bridge, &HarmonyBluetoothBridge::deviceDiscovered);
    bridge.startScan();
    QVERIFY(guard.provider->has(Operation::StartScan));
    bridge.stopScan();
    QVERIFY(!bridge.isScanning());
    QVERIFY(guard.provider->has(Operation::StopScan));
    HarmonyBluetoothProvider::Result result;
    result.scanning = true;
    result.devices = {{address, QStringLiteral("late")}};
    guard.provider->finish(Operation::StartScan, result);
    QCoreApplication::processEvents();
    QCOMPARE(found.count(), 0);
    QVERIFY(!guard.provider->has(Operation::Scan));
}

void HarmonyBluetoothBridgeTest::_closeDiscardsLateOpenAndRead()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
    QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
    bridge.open(address);
    QVERIFY(guard.provider->has(Operation::Open));
    bridge.close();
    guard.provider->finish(Operation::Open);
    QCoreApplication::processEvents();
    QCOMPARE(connected.count(), 0);
    QVERIFY(!bridge.isOpen());
    bridge.open(address);
    guard.provider->finish(Operation::Open);
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(guard.provider->has(Operation::Read));
    bridge.close();
    HarmonyBluetoothProvider::Result result;
    result.data = QByteArrayLiteral("late telemetry");
    guard.provider->finish(Operation::Read, result);
    QCoreApplication::processEvents();
    QCOMPARE(received.count(), 0);
}

void HarmonyBluetoothBridgeTest::_writesAreOrderedAndChunked()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy sent(&bridge, &HarmonyBluetoothBridge::dataSent);
    QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
    bridge.open(address);
    QVERIFY(guard.provider->has(Operation::Open));
    guard.provider->finish(Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    HarmonyBluetoothProvider::Result result;
    result.data = QByteArrayLiteral("mavlink fragment");
    guard.provider->finish(Operation::Read, result);
    QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received[0][0].toByteArray(), result.data);
    const QByteArray first(5000, 'a');
    const QByteArray second = QByteArrayLiteral("second");
    bridge.write(first);
    bridge.write(second);
    QCOMPARE(guard.provider->request(Operation::Write).data.size(), 4096);
    result = {};
    result.written = 1000;
    guard.provider->finish(Operation::Write, result);
    QTRY_COMPARE(sent.count(), 1);
    QCOMPARE(guard.provider->request(Operation::Write).data, first.mid(1000));
    result.written = 4000;
    guard.provider->finish(Operation::Write, result);
    QTRY_COMPARE(sent.count(), 2);
    QCOMPARE(guard.provider->request(Operation::Write).data, second);
    result.written = second.size();
    guard.provider->finish(Operation::Write, result);
    QTRY_COMPARE(sent.count(), 3);
    QCOMPARE(sent[0][0].toByteArray() + sent[1][0].toByteArray() + sent[2][0].toByteArray(), first + second);
}

void HarmonyBluetoothBridgeTest::_zeroWriteRetainsData()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy sent(&bridge, &HarmonyBluetoothBridge::dataSent);
    bridge.open(address);
    QVERIFY(guard.provider->has(Operation::Open));
    guard.provider->finish(Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    const QByteArray data = QByteArrayLiteral("queued");
    bridge.write(data);
    guard.provider->finish(Operation::Write);
    QTRY_VERIFY(guard.provider->has(Operation::Write));
    QCOMPARE(guard.provider->request(Operation::Write).data, data);
    QCOMPARE(sent.count(), 0);
    bridge.close();
    HarmonyBluetoothProvider::Result result;
    result.written = data.size();
    guard.provider->finish(Operation::Write, result);
    QCoreApplication::processEvents();
    QCOMPARE(sent.count(), 0);
}

void HarmonyBluetoothBridgeTest::_errorsCloseTheSession()
{
    ProviderGuard guard;
    HarmonyBluetoothBridge bridge;
    QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
    QSignalSpy disconnected(&bridge, &HarmonyBluetoothBridge::disconnected);
    bridge.open(address);
    QVERIFY(guard.provider->has(Operation::Open));
    HarmonyBluetoothProvider::Result result;
    result.errorCode = 201;
    result.error = QStringLiteral("Bluetooth access was denied");
    guard.provider->finish(Operation::Open, result);
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(errors[0][0].toString(), QStringLiteral("Bluetooth access was denied (201)"));
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(guard.provider->has(Operation::Close));
    QVERIFY(!bridge.isOpen());
}

void HarmonyBluetoothBridgeTest::_destroyWithPendingOperations()
{
    ProviderGuard guard;
    auto *bridge = new HarmonyBluetoothBridge;
    bridge->open(address);
    bridge->startScan();
    QVERIFY(guard.provider->has(Operation::Open));
    QVERIFY(guard.provider->has(Operation::StartScan));
    delete bridge;
    QVERIFY(guard.provider->has(Operation::Close));
    QVERIFY(guard.provider->has(Operation::StopScan));
    guard.provider->finish(Operation::Open);
    guard.provider->finish(Operation::StartScan);
    QCoreApplication::processEvents();
    QVERIFY(!guard.provider->has(Operation::Read));
    QVERIFY(!guard.provider->has(Operation::Scan));
}

void HarmonyBluetoothBridgeTest::_capabilityDistinguishesFactoryFromHardware()
{
    ProviderGuard guard;
    bool ready = false;
    QVERIFY(HarmonyBluetoothBridge::isSupported(nullptr, &ready));
    QVERIFY(ready);
    guard.provider->available = false;
    QVERIFY(!HarmonyBluetoothBridge::isSupported(nullptr, &ready));
    QVERIFY(ready);
    guard.provider->ready = false;
    QVERIFY(!HarmonyBluetoothBridge::isSupported(nullptr, &ready));
    QVERIFY(!ready);
}

void HarmonyBluetoothBridgeTest::_realProviderFactory()
{
    HarmonyBluetoothBridge::setTestProvider({});
    QString error;
    bool ready = false;
    const bool supported = HarmonyBluetoothBridge::isSupported(&error, &ready);
    qInfo().noquote() << "Harmony Bluetooth real factoryReady=" << ready << "supported=" << supported << "error=" << error;
#ifdef Q_OS_HARMONY
    QVERIFY2(ready, qPrintable(error));
    if (!supported) { QVERIFY(!error.isEmpty()); }
#else
    QVERIFY(!ready);
    QVERIFY(!supported);
#endif
}

void HarmonyBluetoothBridgeTest::_configurationAndWorkerUseNativeBackend()
{
#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
    ProviderGuard guard;
    BluetoothConfiguration config(QStringLiteral("Bluetooth SPP test"));
    QSignalSpy names(&config, &BluetoothConfiguration::nameListChanged);
    config.startScan();
    QVERIFY(guard.provider->has(Operation::StartScan));
    QVERIFY(config.scanning());
    HarmonyBluetoothProvider::Result result;
    result.devices = {{address, QStringLiteral("SPP telemetry")}};
    result.scanning = true;
    guard.provider->finish(Operation::StartScan, result);
    QTRY_COMPARE(config.nameList(), QStringList{QStringLiteral("SPP telemetry (%1)").arg(address)});
    config.setDevice(QStringLiteral("SPP telemetry"));
    QCOMPARE(config.deviceName(), QStringLiteral("SPP telemetry"));
    QCOMPARE(config.address(), address);
    config.stopScan();
    QVERIFY(!config.scanning());
    QVERIFY(names.count() >= 2);

    BluetoothWorker worker(&config);
    QSignalSpy connected(&worker, &BluetoothWorker::connected);
    QSignalSpy disconnected(&worker, &BluetoothWorker::disconnected);
    QSignalSpy received(&worker, &BluetoothWorker::dataReceived);
    QSignalSpy sent(&worker, &BluetoothWorker::dataSent);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(guard.provider->has(Operation::Open));
    QCOMPARE(guard.provider->request(Operation::Open).address, address);
    QVERIFY(!worker.isConnected());
    guard.provider->finish(Operation::Open);
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(worker.isConnected());
    result = {};
    result.data = QByteArrayLiteral("telemetry");
    guard.provider->finish(Operation::Read, result);
    QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received[0][0].toByteArray(), result.data);
    worker.writeData(QByteArrayLiteral("command"));
    QVERIFY(guard.provider->has(Operation::Write));
    result = {};
    result.written = 7;
    guard.provider->finish(Operation::Write, result);
    QTRY_COMPARE(sent.count(), 1);
    QCOMPARE(sent[0][0].toByteArray(), QByteArrayLiteral("command"));
    worker.disconnectLink();
    QTRY_COMPARE(disconnected.count(), 1);
    QVERIFY(!worker.isConnected());
    QVERIFY(guard.provider->has(Operation::Close));
#else
    QSKIP("Harmony native Bluetooth integration requires the Bluetooth link build");
#endif
}

void HarmonyBluetoothBridgeTest::_linkThreadPublishesConnectionState()
{
#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
    ProviderGuard guard;
    auto configuration = std::make_shared<BluetoothConfiguration>(QStringLiteral("Bluetooth threaded state"));
    configuration->startScan();
    HarmonyBluetoothProvider::Result result;
    result.devices = {{address, QStringLiteral("Threaded SPP")}};
    guard.provider->finish(Operation::StartScan, result);
    QTRY_VERIFY(configuration->nameList().contains(QStringLiteral("Threaded SPP (%1)").arg(address)));
    configuration->setDevice(QStringLiteral("Threaded SPP"));
    SharedLinkConfigurationPtr sharedConfiguration = configuration;

    for (int round = 0; round < 8; ++round) {
        BluetoothLink initialLink(sharedConfiguration);
        QThread *const workerThread = initialLink.findChild<QThread*>();
        QVERIFY(workerThread);
        QVERIFY(workerThread != QThread::currentThread());
        for (int query = 0; query < 256; ++query) {
            QVERIFY(!initialLink.isConnected());
            QThread::yieldCurrentThread();
        }
    }

    QCOMPARE(LinkManager::instance()->links().count(), 0);
    QVERIFY(LinkManager::instance()->createConnectedLink(sharedConfiguration));
    const auto link = LinkManager::instance()->links().first();
    QVERIFY(qobject_cast<BluetoothLink*>(link.get()));
    QSignalSpy connected(link.get(), &LinkInterface::connected);
    QSignalSpy disconnected(link.get(), &LinkInterface::disconnected);
    QVERIFY(!link->isConnected());
    QTRY_VERIFY(guard.provider->has(Operation::Open));
    QVERIFY(!link->isConnected());
    guard.provider->finish(Operation::Open);
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(link->isConnected());
    link->disconnect();
    QTRY_COMPARE(disconnected.count(), 1);
    QVERIFY(!link->isConnected());
    QVERIFY(guard.provider->has(Operation::Close));
    QTRY_VERIFY(LinkManager::instance()->links().isEmpty());
#else
    QSKIP("Harmony native Bluetooth integration requires the Bluetooth link build");
#endif
}
