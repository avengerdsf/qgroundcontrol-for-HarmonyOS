#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>

#include "HarmonyBluetoothBridge.h"

using Operation = HarmonyBluetoothProvider::Operation;

class TestBluetoothProvider final : public HarmonyBluetoothProvider
{
public:
    QList<Request> requests;
    QList<Completion> completions;

    bool supported(QString*, bool* ready) override
    {
        if (ready) {
            *ready = true;
        }
        return true;
    }

    void call(const Request& request, Completion completion) override
    {
        requests.append(request);
        completions.append(std::move(completion));
    }

    Request take(Operation operation, Result result = {})
    {
        const auto found = std::find_if(requests.cbegin(), requests.cend(),
                                        [operation](const Request& request) { return request.operation == operation; });
        if (found == requests.cend()) {
            qFatal("Expected Bluetooth request is missing");
        }
        const auto index = std::distance(requests.cbegin(), found);
        const auto request = requests.takeAt(index);
        auto callback = completions.takeAt(index);
        callback(std::move(result));
        return request;
    }

    bool has(Operation operation) const
    {
        return std::any_of(requests.cbegin(), requests.cend(),
                           [operation](const Request& request) { return request.operation == operation; });
    }

    Request pending(Operation operation) const
    {
        const auto found = std::find_if(requests.cbegin(), requests.cend(),
                                        [operation](const Request& request) { return request.operation == operation; });
        if (found == requests.cend()) {
            qFatal("Expected pending Bluetooth request is missing");
        }
        return *found;
    }
};

class HarmonyBluetoothHostTest : public QObject
{
    Q_OBJECT
private slots:

    void cleanup() { HarmonyBluetoothBridge::setTestProvider({}); }

    void classicDefaults()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
        bridge.open(QStringLiteral("01:02:03:04:05:06"));
        QVERIFY(!provider->take(Operation::Open).options.lowEnergy);
        QTRY_COMPARE(connected.count(), 1);
        QTRY_VERIFY(provider->has(Operation::Read));
    }

    void bleOptionsReachProvider()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions options;
        options.lowEnergy = true;
        options.serviceUuid = QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb");
        options.writeUuid = options.notifyUuid = QStringLiteral("0000ffe1-0000-1000-8000-00805f9b34fb");
        bridge.open(QStringLiteral("01:02:03:04:05:06"), options);
        const auto request = provider->take(Operation::Open);
        QVERIFY(request.options.lowEnergy);
        QCOMPARE(request.options.serviceUuid, options.serviceUuid);
        QCOMPARE(request.options.writeUuid, options.writeUuid);
        QCOMPARE(request.options.notifyUuid, options.notifyUuid);
    }

    void independentScanAndSocketTransports()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions ble;
        ble.lowEnergy = true;
        bridge.startScan(ble);
        bridge.open(QStringLiteral("01:02:03:04:05:06"));
        QVERIFY(provider->take(Operation::StartScan).options.lowEnergy);
        QVERIFY(!provider->take(Operation::Open).options.lowEnergy);
        QTRY_VERIFY(bridge.isOpen());
        bridge.stopScan();
        QVERIFY(provider->take(Operation::StopScan).options.lowEnergy);
        bridge.close();
        QVERIFY(!provider->take(Operation::Close).options.lowEnergy);
    }

    void lateBleReadCannotEnterReopenedClassicSession()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions ble;
        ble.lowEnergy = true;
        QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
        bridge.open(QStringLiteral("01:02:03:04:05:06"), ble);
        provider->take(Operation::Open);
        QTRY_VERIFY(provider->has(Operation::Read));
        bridge.close();
        QVERIFY(provider->take(Operation::Close).options.lowEnergy);
        bridge.open(QStringLiteral("01:02:03:04:05:07"));
        provider->take(Operation::Open);
        QTRY_VERIFY(bridge.isOpen());
        HarmonyBluetoothProvider::Result oldRead;
        oldRead.data = QByteArray::fromHex("fd01");
        provider->take(Operation::Read, oldRead);
        QCoreApplication::processEvents();
        QCOMPARE(received.count(), 0);
        HarmonyBluetoothProvider::Result newRead;
        newRead.data = QByteArray::fromHex("fe02");
        provider->take(Operation::Read, newRead);
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received[0][0].toByteArray(), newRead.data);
    }

    void blockedTransportHasBoundedTransmitQueue()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions ble;
        ble.lowEnergy = true;
        bridge.open(QStringLiteral("01:02:03:04:05:06"), ble);
        provider->take(Operation::Open);
        QTRY_VERIFY(bridge.isOpen());
        QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
        bridge.write(QByteArray(256 * 1024, 'x'));
        QVERIFY(provider->has(Operation::Write));
        bridge.write(QByteArray(1, 'y'));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!bridge.isOpen());
        QVERIFY(provider->take(Operation::Close).options.lowEnergy);
    }

    void partialWritesPreserveOrderAndReleaseQueueCapacity()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions ble;
        ble.lowEnergy = true;
        bridge.open(QStringLiteral("01:02:03:04:05:06"), ble);
        provider->take(Operation::Open);
        QTRY_VERIFY(bridge.isOpen());
        QSignalSpy sent(&bridge, &HarmonyBluetoothBridge::dataSent);
        QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
        QByteArray payload(9000, Qt::Uninitialized);
        for (qsizetype index = 0; index < payload.size(); ++index) {
            payload[index] = char(index % 251);
        }
        const QByteArray tail = QByteArray::fromHex("fdfe0102");
        const QByteArray expected = payload + tail;
        bridge.write(payload);
        bridge.write(tail);
        qsizetype completed = 0;
        int operations = 0;
        while (completed < expected.size()) {
            QTRY_VERIFY(provider->has(Operation::Write));
            const auto pending = provider->pending(Operation::Write);
            QVERIFY(pending.options.lowEnergy);
            QVERIFY(pending.data.size() <= 4096);
            const qint64 amount = operations == 0 ? 1000 : pending.data.size();
            HarmonyBluetoothProvider::Result result;
            result.written = amount;
            provider->take(Operation::Write, result);
            completed += amount;
            ++operations;
        }
        QTRY_COMPARE(sent.count(), operations);
        QByteArray actual;
        for (const auto& signal : sent) {
            actual.append(signal[0].toByteArray());
        }
        QCOMPARE(actual, expected);
        bridge.write(QByteArray(256 * 1024, 'x'));
        QCOMPARE(errors.count(), 0);
        QVERIFY(bridge.isOpen());
    }

    void pendingOpenCannotPublishSuccessAfterClose()
    {
        auto provider = std::make_shared<TestBluetoothProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        HarmonyBluetoothBridge bridge;
        HarmonyBluetoothOptions ble;
        ble.lowEnergy = true;
        QSignalSpy connected(&bridge, &HarmonyBluetoothBridge::connected);
        bridge.open(QStringLiteral("01:02:03:04:05:06"), ble);
        bridge.close();
        QVERIFY(provider->take(Operation::Close).options.lowEnergy);
        provider->take(Operation::Open);
        QCoreApplication::processEvents();
        QCOMPARE(connected.count(), 0);
        QVERIFY(!provider->has(Operation::Read));
    }

    void receiveErrorAndOversizedDataCloseTheSelectedTransport()
    {
        for (bool oversized : {false, true}) {
            auto provider = std::make_shared<TestBluetoothProvider>();
            HarmonyBluetoothBridge::setTestProvider(provider);
            HarmonyBluetoothBridge bridge;
            HarmonyBluetoothOptions ble;
            ble.lowEnergy = true;
            bridge.open(QStringLiteral("01:02:03:04:05:06"), ble);
            provider->take(Operation::Open);
            QTRY_VERIFY(provider->has(Operation::Read));
            QSignalSpy received(&bridge, &HarmonyBluetoothBridge::dataReceived);
            QSignalSpy errors(&bridge, &HarmonyBluetoothBridge::errorOccurred);
            HarmonyBluetoothProvider::Result result;
            if (oversized) {
                result.data = QByteArray(8193, 'x');
            } else {
                result.errorCode = 2901003;
                result.error = QStringLiteral("Peer disconnected");
            }
            provider->take(Operation::Read, result);
            QTRY_COMPARE(errors.count(), 1);
            QCOMPARE(received.count(), 0);
            QVERIFY(!bridge.isOpen());
            QVERIFY(provider->take(Operation::Close).options.lowEnergy);
        }
    }
};

QTEST_GUILESS_MAIN(HarmonyBluetoothHostTest)
#include "HarmonyBluetoothHostTest.moc"
