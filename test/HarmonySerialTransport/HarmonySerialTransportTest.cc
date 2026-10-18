#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <utility>

#include "HarmonySerialBridge.h"

class SerialProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString*, bool*) override { return {}; }

    void call(const Request& request, Completion completion) override
    {
        switch (request.operation) {
            case Operation::Open:
                openSettings = request.settings;
                openDone = std::move(completion);
                break;
            case Operation::Read:
                readDone = std::move(completion);
                break;
            case Operation::Write:
                writes.append(request.data);
                writeDone = std::move(completion);
                break;
            default:
                completion({});
                break;
        }
    }

    void finishWrite(qint64 size)
    {
        Result result;
        result.written = size;
        auto callback = std::exchange(writeDone, {});
        callback(std::move(result));
    }

    HarmonySerialSettings openSettings;
    QList<QByteArray> writes;
    Completion openDone, readDone, writeDone;
};

class ProviderGuard
{
public:
    ProviderGuard() : provider(std::make_shared<SerialProvider>()) { HarmonySerialBridge::setTestProvider(provider); }

    ~ProviderGuard() { HarmonySerialBridge::setTestProvider({}); }

    std::shared_ptr<SerialProvider> provider;
};

class HarmonySerialTransportTest : public QObject
{
    Q_OBJECT
private slots:

    void dtrConfigurationIsForwarded_data()
    {
        QTest::addColumn<bool>("forceLow");
        QTest::newRow("assert-DTR") << false;
        QTest::newRow("force-DTR-low") << true;
    }

    void dtrConfigurationIsForwarded()
    {
        QFETCH(bool, forceLow);
        ProviderGuard guard;
        HarmonySerialBridge bridge;
        HarmonySerialSettings settings;
        settings.dtrForceLow = forceLow;
        bridge.open(QStringLiteral("harmony-serial:42"), settings);
        QCOMPARE(guard.provider->openSettings.dtrForceLow, forceLow);
    }

    void burst_data()
    {
        QTest::addColumn<int>("packets");
        QTest::addColumn<int>("bytes");
        QTest::addColumn<int>("calls");
        QTest::newRow("64-small-packets") << 64 << 32 << 2;
        QTest::newRow("bounded-4096-byte-batches") << 128 << 128 << 5;
    }

    void burst()
    {
        QFETCH(int, packets);
        QFETCH(int, bytes);
        QFETCH(int, calls);
        ProviderGuard guard;
        HarmonySerialBridge bridge;
        QSignalSpy sent(&bridge, &HarmonySerialBridge::dataSent);
        bridge.open(QStringLiteral("harmony-serial:42"), {});
        guard.provider->openDone({});
        QTRY_VERIFY(bridge.isOpen());
        QByteArray expected, actual;
        for (int index = 0; index < packets; ++index) {
            const QByteArray packet(bytes, static_cast<char>(index));
            expected += packet;
            bridge.write(packet);
        }
        for (int count = 0; actual.size() < expected.size() && count <= packets; ++count) {
            QTRY_VERIFY(static_cast<bool>(guard.provider->writeDone));
            const QByteArray payload = guard.provider->writes.last();
            QVERIFY(payload.size() <= 4096);
            actual += payload;
            guard.provider->finishWrite(payload.size());
            QCoreApplication::processEvents();
        }
        QCOMPARE(actual, expected);
        QByteArray signalled;
        for (const auto& event : sent) {
            signalled += event[0].toByteArray();
        }
        QCOMPARE(signalled, expected);
        QCOMPARE(guard.provider->writes.size(), calls);
    }

    void partialWriteCrossesPacketBoundary()
    {
        ProviderGuard guard;
        HarmonySerialBridge bridge;
        QSignalSpy sent(&bridge, &HarmonySerialBridge::dataSent);
        bridge.open(QStringLiteral("harmony-serial:42"), {});
        guard.provider->openDone({});
        QTRY_VERIFY(bridge.isOpen());
        bridge.write(QByteArrayLiteral("prefix"));
        bridge.write(QByteArray(10, 'a'));
        bridge.write(QByteArray(10, 'b'));
        bridge.write(QByteArray(10, 'c'));
        guard.provider->finishWrite(6);
        QTRY_COMPARE(guard.provider->writes.size(), 2);
        QCOMPARE(guard.provider->writes.last(), QByteArray(10, 'a') + QByteArray(10, 'b') + QByteArray(10, 'c'));
        guard.provider->finishWrite(12);
        QTRY_COMPARE(guard.provider->writes.size(), 3);
        QCOMPARE(guard.provider->writes.last(), QByteArray(8, 'b') + QByteArray(10, 'c'));
        guard.provider->finishWrite(18);
        QTRY_COMPARE(sent.count(), 3);
        QCOMPARE(sent[0][0].toByteArray() + sent[1][0].toByteArray() + sent[2][0].toByteArray(),
                 QByteArrayLiteral("prefix") + QByteArray(10, 'a') + QByteArray(10, 'b') + QByteArray(10, 'c'));
    }

    void zeroWriteRetainsPendingBurst()
    {
        ProviderGuard guard;
        HarmonySerialBridge bridge;
        QSignalSpy sent(&bridge, &HarmonySerialBridge::dataSent);
        bridge.open(QStringLiteral("harmony-serial:42"), {});
        guard.provider->openDone({});
        QTRY_VERIFY(bridge.isOpen());
        bridge.write(QByteArrayLiteral("first"));
        bridge.write(QByteArrayLiteral("second"));
        guard.provider->finishWrite(0);
        QTRY_COMPARE(guard.provider->writes.size(), 2);
        QCOMPARE(sent.count(), 0);
        QCOMPARE(guard.provider->writes.last(), QByteArrayLiteral("firstsecond"));
        guard.provider->finishWrite(11);
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent[0][0].toByteArray(), QByteArrayLiteral("firstsecond"));
    }

    void closeCancelsPendingRetry()
    {
        ProviderGuard guard;
        HarmonySerialBridge bridge;
        bridge.open(QStringLiteral("harmony-serial:42"), {});
        guard.provider->openDone({});
        QTRY_VERIFY(bridge.isOpen());
        bridge.write(QByteArrayLiteral("pending"));
        guard.provider->finishWrite(0);
        QCoreApplication::processEvents();
        bridge.close();
        QTest::qWait(60);
        QCOMPARE(guard.provider->writes.size(), 1);
        QVERIFY(!bridge.isOpen());
    }
};

QTEST_GUILESS_MAIN(HarmonySerialTransportTest)
#include "HarmonySerialTransportTest.moc"
