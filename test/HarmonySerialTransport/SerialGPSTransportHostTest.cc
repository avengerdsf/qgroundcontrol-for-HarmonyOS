#include <QtCore/QElapsedTimer>
#include <QtCore/QMutexLocker>
#include <QtTest/QTest>
#include <future>

#include "HarmonySerialBridge.h"
#include "SerialGPSTransport.h"

class GpsProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString*, bool*) override { return {}; }

    void call(const Request& request, Completion completion) override
    {
        Result result;
        {
            const QMutexLocker locker(&mutex);
            if (request.operation == Operation::Read) {
                readCompletion = std::move(completion);
                return;
            }
            if (request.operation == Operation::Open) {
                automatic = request.settings.autoConnect;
            }
            if (request.operation == Operation::SetBaud || request.operation == Operation::Open) {
                baud = request.settings.baud;
            }
            if (request.operation == Operation::Write) {
                result.written = request.data.size();
                written += request.data;
            }
        }
        completion(std::move(result));
    }

    bool waiting() const
    {
        const QMutexLocker locker(&mutex);
        return bool(readCompletion);
    }

    void deliver(const QByteArray& data, bool fail = false)
    {
        Completion completion;
        {
            const QMutexLocker locker(&mutex);
            completion = std::move(readCompletion);
        }
        Result result;
        result.data = data;
        if (fail) {
            result.error = QStringLiteral("USB disconnected");
        }
        if (completion) {
            completion(std::move(result));
        }
    }

    mutable QMutex mutex;
    Completion readCompletion;
    QByteArray written;
    qint32 baud = 0;
    bool automatic = false;
};

class SerialGPSTransportHostTest : public QObject
{
    Q_OBJECT
    std::shared_ptr<GpsProvider> provider;
private slots:

    void init()
    {
        provider = std::make_shared<GpsProvider>();
        HarmonySerialBridge::setTestProvider(provider);
    }

    void cleanup() { HarmonySerialBridge::setTestProvider({}); }

    void serialTransportReadsWritesAndSetsBaud()
    {
        auto work = std::async(std::launch::async, []() {
            std::atomic_bool stop = false;
            SerialGPSTransport transport(QStringLiteral("harmony-serial:42"), stop);
            if (!transport.open() || !transport.setBaudrate(115200)) {
                return QByteArray{};
            }
            const uint8_t command[] = {0xb5, 0x62, 0x06};
            if (transport.write(command, sizeof(command)) != sizeof(command)) {
                return QByteArray{};
            }
            uint8_t buffer[64];
            const int size = transport.read(buffer, sizeof(buffer), 1000);
            return size > 0 ? QByteArray(reinterpret_cast<char*>(buffer), size) : QByteArray{};
        });
        QTRY_VERIFY_WITH_TIMEOUT(provider->waiting(), 1000);
        provider->deliver(QByteArray::fromHex("d30003414243"));
        QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 2000);
        QCOMPARE(work.get(), QByteArray::fromHex("d30003414243"));
        const QMutexLocker locker(&provider->mutex);
        QCOMPARE(provider->baud, 115200);
        QVERIFY(provider->automatic);
        QCOMPARE(provider->written, QByteArray::fromHex("b56206"));
    }

    void stopInterruptsAnInflightRead()
    {
        std::atomic_bool stop = false;
        std::atomic_bool reading = false;
        auto work = std::async(std::launch::async, [&]() {
            SerialGPSTransport transport(QStringLiteral("harmony-serial:42"), stop);
            if (!transport.open()) {
                return 0;
            }
            uint8_t buffer[64];
            reading = true;
            return transport.read(buffer, sizeof(buffer), 10000);
        });
        QTRY_VERIFY_WITH_TIMEOUT(reading.load(), 1000);
        stop = true;
        QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 500);
        QCOMPARE(work.get(), -1);
    }

    void unplugIsReportedAsTerminalReadError()
    {
        std::atomic_bool stop = false;
        auto work = std::async(std::launch::async, [&]() {
            SerialGPSTransport transport(QStringLiteral("harmony-serial:42"), stop);
            if (!transport.open()) {
                return false;
            }
            uint8_t buffer[64];
            return transport.read(buffer, sizeof(buffer), 10000) < 0 && transport.fatalError();
        });
        QTRY_VERIFY_WITH_TIMEOUT(provider->waiting(), 1000);
        provider->deliver({}, true);
        QTRY_VERIFY_WITH_TIMEOUT(work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 1000);
        QVERIFY(work.get());
    }
};
QTEST_GUILESS_MAIN(SerialGPSTransportHostTest)
#include "SerialGPSTransportHostTest.moc"
