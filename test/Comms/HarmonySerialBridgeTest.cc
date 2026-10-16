#include "HarmonySerialBridgeTest.h"
#include "HarmonySerialBridge.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "SerialLink.h"
#include "QGCSerialPortInfo.h"
#include "Vehicle.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>

namespace {
class SerialTestProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString *, bool *factoryReady) override
    {
        if (factoryReady) { *factoryReady = true; }
        return {{42, QStringLiteral("test serial")}};
    }

    void call(const Request &request, Completion completion) override
    {
        const QMutexLocker locker(&mutex);
        requests.append(request);
        callbacks.append(std::move(completion));
    }

    int pending() const { const QMutexLocker locker(&mutex); return requests.size(); }
    Request first() const { const QMutexLocker locker(&mutex); return requests.first(); }
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
    void finish(const Result &result = {})
    {
        Completion completion;
        {
            const QMutexLocker locker(&mutex);
            requests.removeFirst();
            completion = callbacks.takeFirst();
        }
        completion(result);
    }

    mutable QMutex mutex;
    QList<Request> requests;
    QList<Completion> callbacks;
};

class ProviderGuard
{
public:
    ProviderGuard() : provider(std::make_shared<SerialTestProvider>()) { HarmonySerialBridge::setTestProvider(provider); }
    ~ProviderGuard() { HarmonySerialBridge::setTestProvider({}); }
    std::shared_ptr<SerialTestProvider> provider;
};
}

void HarmonySerialBridgeTest::_authorizationIsAsynchronous()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy connected(&bridge, &HarmonySerialBridge::connected);
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    QCOMPARE(guard.provider->pending(), 1);
    QVERIFY(!bridge.isOpen());
    QCOMPARE(connected.count(), 0);
    guard.provider->finish();
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(bridge.isOpen());
    QTRY_COMPARE(guard.provider->first().operation, HarmonySerialProvider::Operation::Read);
}

void HarmonySerialBridgeTest::_officialBaudRatesAccepted_data()
{
    QTest::addColumn<qint32>("baud");
    const QStringList rates = SerialConfiguration::supportedBaudRates();
    QCOMPARE(rates.size(), 19);
    for (const QString &rate : rates) {
        QTest::newRow(qPrintable(rate)) << rate.toInt();
    }
}

void HarmonySerialBridgeTest::_officialBaudRatesAccepted()
{
    QFETCH(qint32, baud);
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy errors(&bridge, &HarmonySerialBridge::errorOccurred);
    HarmonySerialSettings settings;
    settings.baud = baud;
    bridge.open(QStringLiteral("harmony-serial:42"), settings);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Open));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Open).settings.baud, baud);
    guard.provider->finish(HarmonySerialProvider::Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    bridge.setBaudRate(baud);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::SetBaud));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::SetBaud).settings.baud, baud);
    guard.provider->finish(HarmonySerialProvider::Operation::SetBaud);
    QCoreApplication::processEvents();
    QCOMPARE(errors.count(), 0);
}

void HarmonySerialBridgeTest::_lateOpenAndReadAreDiscarded()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy connected(&bridge, &HarmonySerialBridge::connected);
    QSignalSpy received(&bridge, &HarmonySerialBridge::dataReceived);
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Open));
    bridge.close();
    guard.provider->finish();
    QCoreApplication::processEvents();
    QCOMPARE(connected.count(), 0);
    QVERIFY(!bridge.isOpen());
    while (guard.provider->pending()) { guard.provider->finish(); }

    bridge.open(QStringLiteral("harmony-serial:42"), {});
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Open));
    guard.provider->finish();
    QTRY_VERIFY(bridge.isOpen());
    QTRY_VERIFY(guard.provider->pending() > 0);
    bridge.close();
    HarmonySerialProvider::Result result;
    result.data = QByteArrayLiteral("late bytes");
    guard.provider->finish(result);
    QCoreApplication::processEvents();
    QCOMPARE(received.count(), 0);
    QVERIFY(!bridge.isOpen());
}

void HarmonySerialBridgeTest::_partialWritesAreOrderedAndChunked()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy sent(&bridge, &HarmonySerialBridge::dataSent);
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Open));
    guard.provider->finish();
    QTRY_VERIFY(bridge.isOpen());
    const QByteArray first(5000, 'a');
    const QByteArray second = QByteArrayLiteral("second");
    bridge.write(first);
    bridge.write(second);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data.size(), 4096);
    HarmonySerialProvider::Result partial;
    partial.written = 1000;
    guard.provider->finish(HarmonySerialProvider::Operation::Write, partial);
    QTRY_COMPARE(sent.count(), 1);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, first.mid(1000, 4000) + second);
    partial.written = 4000;
    guard.provider->finish(HarmonySerialProvider::Operation::Write, partial);
    QTRY_COMPARE(sent.count(), 2);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, second);
    partial.written = second.size();
    guard.provider->finish(HarmonySerialProvider::Operation::Write, partial);
    QTRY_COMPARE(sent.count(), 3);
    QCOMPARE(sent[0][0].toByteArray() + sent[1][0].toByteArray() + sent[2][0].toByteArray(), first + second);
}

void HarmonySerialBridgeTest::_zeroWriteRetainsQueuedData()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy errors(&bridge, &HarmonySerialBridge::errorOccurred);
    QSignalSpy disconnected(&bridge, &HarmonySerialBridge::disconnected);
    QSignalSpy sent(&bridge, &HarmonySerialBridge::dataSent);
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Open));
    guard.provider->finish();
    QTRY_VERIFY(bridge.isOpen());
    const QByteArray first(5000, 'a');
    const QByteArray second = QByteArrayLiteral("second");
    bridge.write(first);
    bridge.write(second);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, first.first(4096));
    guard.provider->finish(HarmonySerialProvider::Operation::Write);
    QCoreApplication::processEvents();
    QVERIFY(bridge.isOpen());
    QCOMPARE(errors.count(), 0);
    QCOMPARE(disconnected.count(), 0);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, first.first(4096));
    HarmonySerialProvider::Result result;
    result.written = 4096;
    guard.provider->finish(HarmonySerialProvider::Operation::Write, result);
    QTRY_COMPARE(sent.count(), 1);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, first.mid(4096) + second);
    result.written = first.size() - 4096;
    guard.provider->finish(HarmonySerialProvider::Operation::Write, result);
    QTRY_COMPARE(sent.count(), 2);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    QCOMPARE(guard.provider->request(HarmonySerialProvider::Operation::Write).data, second);
    result.written = second.size();
    guard.provider->finish(HarmonySerialProvider::Operation::Write, result);
    QTRY_COMPARE(sent.count(), 3);
    QCOMPARE(sent[0][0].toByteArray() + sent[1][0].toByteArray() + sent[2][0].toByteArray(), first + second);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(disconnected.count(), 0);
}

void HarmonySerialBridgeTest::_closeCancelsZeroWriteRetry()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    guard.provider->finish(HarmonySerialProvider::Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    bridge.write(QByteArrayLiteral("pending"));
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    guard.provider->finish(HarmonySerialProvider::Operation::Write);
    QCoreApplication::processEvents();
    QVERIFY(bridge.isOpen());
    bridge.close();
    QTest::qWait(150);
    QVERIFY(!guard.provider->has(HarmonySerialProvider::Operation::Write));
    QVERIFY(!bridge.isOpen());
}

void HarmonySerialBridgeTest::_unsupportedSettingsAreRejected()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy errors(&bridge, &HarmonySerialBridge::errorOccurred);
    HarmonySerialSettings settings;
    settings.flowControl = QSerialPort::HardwareControl;
    bridge.open(QStringLiteral("harmony-serial:42"), settings);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors[0][0].toString().contains(QStringLiteral("flow control")));
    QCOMPARE(guard.provider->pending(), 0);
    settings.flowControl = QSerialPort::NoFlowControl;
    settings.stopBits = QSerialPort::OneAndHalfStop;
    bridge.open(QStringLiteral("harmony-serial:42"), settings);
    QCOMPARE(errors.count(), 2);
    QVERIFY(errors[1][0].toString().contains(QStringLiteral("stop bits")));
    QCOMPARE(guard.provider->pending(), 0);
}

void HarmonySerialBridgeTest::_destroyDuringPendingOperations()
{
    ProviderGuard guard;
    auto bridge = std::make_unique<HarmonySerialBridge>();
    int received = 0;
    int sent = 0;
    (void) connect(bridge.get(), &HarmonySerialBridge::dataReceived, this, [&received]() { ++received; });
    (void) connect(bridge.get(), &HarmonySerialBridge::dataSent, this, [&sent]() { ++sent; });
    bridge->open(QStringLiteral("harmony-serial:42"), {});
    guard.provider->finish(HarmonySerialProvider::Operation::Open);
    QTRY_VERIFY(bridge->isOpen());
    bridge->write(QByteArrayLiteral("pending"));
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Read));
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Write));
    bridge.reset();
    HarmonySerialProvider::Result result;
    result.data = QByteArrayLiteral("late");
    result.written = 7;
    guard.provider->finish(HarmonySerialProvider::Operation::Read, result);
    guard.provider->finish(HarmonySerialProvider::Operation::Write, result);
    QCoreApplication::processEvents();
    QCOMPARE(received, 0);
    QCOMPARE(sent, 0);
}

void HarmonySerialBridgeTest::_reconnectDiscardsPreviousSession()
{
    ProviderGuard guard;
    HarmonySerialBridge bridge;
    QSignalSpy received(&bridge, &HarmonySerialBridge::dataReceived);
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    guard.provider->finish(HarmonySerialProvider::Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Read));
    const quint64 previous = guard.provider->request(HarmonySerialProvider::Operation::Read).session;
    bridge.close();
    bridge.open(QStringLiteral("harmony-serial:42"), {});
    guard.provider->finish(HarmonySerialProvider::Operation::Open);
    QTRY_VERIFY(bridge.isOpen());
    HarmonySerialProvider::Result result;
    result.data = QByteArrayLiteral("previous session");
    guard.provider->finish(HarmonySerialProvider::Operation::Read, result);
    QCoreApplication::processEvents();
    QCOMPARE(received.count(), 0);
    QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Read));
    QVERIFY(guard.provider->request(HarmonySerialProvider::Operation::Read).session != previous);
    result.data = QByteArrayLiteral("current session");
    guard.provider->finish(HarmonySerialProvider::Operation::Read, result);
    QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received[0][0].toByteArray(), result.data);
}

void HarmonySerialBridgeTest::_fragmentedHeartbeatCreatesVehicle()
{
    ProviderGuard guard;
    QCOMPARE(LinkManager::instance()->links().count(), 0);
    QCOMPARE(MultiVehicleManager::instance()->vehicles()->count(), 0);
    auto configuration = std::make_shared<SerialConfiguration>(QStringLiteral("Harmony test serial"));
    configuration->setPortName(QStringLiteral("harmony-serial:42"));
    SharedLinkConfigurationPtr sharedConfiguration = configuration;
    LinkManager::instance()->createConnectedLink(sharedConfiguration);
    QTRY_VERIFY(guard.provider->pending() > 0);
    QCOMPARE(guard.provider->first().operation, HarmonySerialProvider::Operation::Open);
    guard.provider->finish();
    QTRY_VERIFY(guard.provider->pending() > 0);
    QTRY_VERIFY(!LinkManager::instance()->links().isEmpty());
    const SerialLink *const serial = qobject_cast<const SerialLink *>(LinkManager::instance()->links().first().get());
    QVERIFY(serial);
    QVERIFY(!serial->port());

    mavlink_message_t message{};
    mavlink_msg_heartbeat_pack(76, MAV_COMP_ID_AUTOPILOT1, &message, MAV_TYPE_QUADROTOR,
        MAV_AUTOPILOT_PX4, 0, 0, MAV_STATE_STANDBY);
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
    const int length = mavlink_msg_to_send_buffer(buffer, &message);
    const QByteArray heartbeat(reinterpret_cast<const char *>(buffer), length);
    for (const QByteArray &fragment : {heartbeat.first(3), heartbeat.mid(3, 5), heartbeat.mid(8)}) {
        QTRY_VERIFY(guard.provider->has(HarmonySerialProvider::Operation::Read));
        HarmonySerialProvider::Result result;
        result.data = fragment;
        guard.provider->finish(HarmonySerialProvider::Operation::Read, result);
        QCoreApplication::processEvents();
    }
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle() != nullptr, 2000);
    QCOMPARE(MultiVehicleManager::instance()->activeVehicle()->id(), 76);
    LinkManager::instance()->disconnectAll();
    QTRY_COMPARE(MultiVehicleManager::instance()->vehicles()->count(), 0);
}

void HarmonySerialBridgeTest::_realProviderEnumeration()
{
    HarmonySerialBridge::setTestProvider({});
    QString error;
    bool factoryReady = false;
    const auto ports = HarmonySerialBridge::availablePorts(&error, &factoryReady);
    qInfo().noquote() << "Harmony USB serial real factoryReady=" << factoryReady << "ports=" << ports.size() << "error=" << error;
    QVERIFY2(factoryReady, qPrintable(error));
    QVERIFY2(error.isEmpty() || error.contains(QStringLiteral("801")), qPrintable(error));
    for (const QGCSerialPortInfo &port : QGCSerialPortInfo::availablePorts()) {
        QVERIFY(!port.isNull());
        QCOMPARE(port.hasVendorIdentifier(), port.hasProductIdentifier());
        QVERIFY(port.serialNumber().isEmpty());
        QGCSerialPortInfo::BoardType_t boardType;
        QString name;
        const bool recognized = port.getBoardInfo(boardType, name);
        if (!port.hasVendorIdentifier()) {
            QVERIFY(!recognized);
            QCOMPARE(boardType, QGCSerialPortInfo::BoardTypeUnknown);
            QVERIFY(!port.canFlash());
        }
    }
}
