#include <QtCore/QMutexLocker>
#include <QtCore/QScopeGuard>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "Fixture.h"

class PendingProvider final : public HarmonySerialProvider
{
public:
    QList<HarmonySerialPortInfo> ports(QString*, bool*) override
    {
        return {{42, QStringLiteral("controlled USB port")}};
    }

    void call(const Request& request, Completion completion) override
    {
        const QMutexLocker locker(&mutex);
        if (request.operation == Operation::Open) {
            open = std::move(completion);
        } else if (request.operation == Operation::Read) {
            ++reads;
        } else {
            if (request.operation == Operation::Close) {
                ++closes;
            }
            completion({});
        }
    }

    bool pending() const
    {
        const QMutexLocker locker(&mutex);
        return bool(open);
    }

    int closeCount() const
    {
        const QMutexLocker locker(&mutex);
        return closes;
    }

    int readCount() const
    {
        const QMutexLocker locker(&mutex);
        return reads;
    }

    void grant()
    {
        Completion completion;
        {
            const QMutexLocker locker(&mutex);
            completion = std::move(open);
        }
        if (completion) {
            completion({});
        }
    }

private:
    mutable QMutex mutex;
    Completion open;
    int closes = 0;
    int reads = 0;
};

class SerialLinkLifecycleTest : public QObject
{
    Q_OBJECT
private slots:

    void pendingOpenDisconnectDiscardsLatePermissionGrant()
    {
        const auto provider = std::make_shared<PendingProvider>();
        HarmonySerialBridge::setTestProvider(provider);
        const auto restoreProvider = qScopeGuard([]() { HarmonySerialBridge::setTestProvider({}); });
        auto serialConfig = std::make_shared<SerialConfiguration>(QStringLiteral("Pending USB permission"));
        serialConfig->setPortName(QStringLiteral("harmony-serial:42"));
        SharedLinkConfigurationPtr config = serialConfig;
        SerialLink link(config);
        QSignalSpy connected(&link, &SerialLink::connected);
        QVERIFY(link._connect());
        QTRY_VERIFY_WITH_TIMEOUT(provider->pending(), 1000);
        QVERIFY(!link.isConnected());
        link.disconnect();
        // A barrier after the public disconnect establishes that its queued close had a chance to run.
        QVERIFY(QMetaObject::invokeMethod(link._worker, []() {}, Qt::BlockingQueuedConnection));
        provider->grant();
        QVERIFY(!connected.wait(200));
        QVERIFY(!link.isConnected());
        QCOMPARE(provider->closeCount(), 1);
        QCOMPARE(provider->readCount(), 0);
    }
};
QTEST_GUILESS_MAIN(SerialLinkLifecycleTest)
#include "SerialLinkLifecycleTest.moc"
