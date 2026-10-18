#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>

#include "BluetoothConfiguration.h"
#include "BluetoothWorker.h"
#include "HarmonyBluetoothBridge.h"

using Operation = HarmonyBluetoothProvider::Operation;

class DiscoveryProvider final : public HarmonyBluetoothProvider
{
public:
    QList<Request> history;
    QList<Request> pending;
    QList<Completion> completions;
    bool classicSupported = true;
    bool bleSupported = true;

    bool supported(QString*, bool* ready) override
    {
        if (ready) {
            *ready = true;
        }
        return classicSupported || bleSupported;
    }

    bool transportSupported(bool lowEnergy, QString* error, bool* ready) override
    {
        if (ready) {
            *ready = true;
        }
        const bool available = lowEnergy ? bleSupported : classicSupported;
        if (error) {
            *error = available ? QString() : QStringLiteral("Bluetooth capability unavailable (801)");
        }
        return available;
    }

    void call(const Request& request, Completion completion) override
    {
        history.append(request);
        if (request.operation == Operation::StopScan || request.operation == Operation::Close) {
            completion({});
        } else {
            pending.append(request);
            completions.append(std::move(completion));
        }
    }

    bool finish(Operation operation, Result result = {})
    {
        for (int i = 0; i < pending.size(); ++i) {
            if (pending[i].operation == operation) {
                pending.removeAt(i);
                completions.takeAt(i)(std::move(result));
                return true;
            }
        }
        return false;
    }
};

class ConfigurationTest : public QObject
{
    Q_OBJECT
private slots:

    void cleanup() { HarmonyBluetoothBridge::setTestProvider({}); }

    void legacyBleSettingsMigrateAndNewSettingsWin()
    {
        QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/settings-XXXXXX"));
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("links.ini"), QSettings::IniFormat);
        settings.setValue("Link/lowEnergy", true);
        settings.setValue("Link/deviceName", "Radio");
        settings.setValue("Link/address", "12:34:56:78:90:AB");
        settings.setValue("Link/bleServiceUuid", " FFE0 ");
        settings.setValue("Link/bleWriteUuid", "FFE1");
        settings.setValue("Link/bleNotifyUuid", "FFE2");
        BluetoothConfiguration config("Legacy");
        config.loadSettings(settings, "Link");
        QCOMPARE(config.mode(), BluetoothConfiguration::BluetoothMode::ModeLowEnergy);
        QCOMPARE(config.serviceUuid(), QStringLiteral("{0000ffe0-0000-1000-8000-00805f9b34fb}"));
        QCOMPARE(config.writeUuid(), QStringLiteral("{0000ffe1-0000-1000-8000-00805f9b34fb}"));
        QCOMPARE(config.readUuid(), QStringLiteral("{0000ffe2-0000-1000-8000-00805f9b34fb}"));
        QCOMPARE(config.device().coreConfigurations(), QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
        config.saveSettings(settings, "Link");
        settings.setValue("Link/lowEnergy", false);
        settings.setValue("Link/bleServiceUuid", "ABCD");
        BluetoothConfiguration restored("Restored");
        restored.loadSettings(settings, "Link");
        QCOMPARE(restored.mode(), config.mode());
        QCOMPARE(restored.serviceUuid(), config.serviceUuid());
        QCOMPARE(restored.address(), config.address());
    }

    void shortUuidsNormalizeAndInvalidUuidDoesNotEraseProfile()
    {
        BluetoothConfiguration config("UUID");
        config.setServiceUuid(" FFE0 ");
        QCOMPARE(config.serviceUuid(), QStringLiteral("{0000ffe0-0000-1000-8000-00805f9b34fb}"));
        QSignalSpy errors(&config, &BluetoothConfiguration::errorOccurred);
        config.setServiceUuid("not-uuid");
        QCOMPARE(errors.count(), 1);
        QCOMPARE(config.serviceUuid(), QStringLiteral("{0000ffe0-0000-1000-8000-00805f9b34fb}"));
    }

    void modeSwitchClearsSelectedDevice()
    {
        BluetoothConfiguration config("Switch");
        QBluetoothDeviceInfo info(QBluetoothAddress("12:34:56:78:90:AB"), "Radio", 0);
        info.setCoreConfigurations(QBluetoothDeviceInfo::BaseRateCoreConfiguration);
        QVERIFY(QMetaObject::invokeMethod(&config, "_deviceDiscovered", Qt::DirectConnection,
                                          Q_ARG(QBluetoothDeviceInfo, info)));
        config.setDeviceByAddress(info.address().toString());
        QCOMPARE(config.address(), info.address().toString());
        config.setMode(BluetoothConfiguration::BluetoothMode::ModeLowEnergy);
        QVERIFY(config.device().address().isNull());
        QVERIFY(config.devicesModel().isEmpty());
    }

    void nativeScanKeepsTransportAndDuplicateAddresses_data()
    {
        QTest::addColumn<bool>("lowEnergy");
        QTest::newRow("SPP") << false;
        QTest::newRow("BLE") << true;
    }

    void nativeScanKeepsTransportAndDuplicateAddresses()
    {
        QFETCH(bool, lowEnergy);
        auto provider = std::make_shared<DiscoveryProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        BluetoothConfiguration config("Scan");
        config.setMode(lowEnergy ? BluetoothConfiguration::BluetoothMode::ModeLowEnergy
                                 : BluetoothConfiguration::BluetoothMode::ModeClassic);
        config.startScan();
        QVERIFY(config.scanning());
        QCOMPARE(provider->history.last().options.lowEnergy, lowEnergy);
        HarmonyBluetoothProvider::Result found;
        found.devices = {{"12:34:56:78:90:AB", "Radio", -40}, {"12:34:56:78:90:AC", "Radio", -70}};
        found.scanning = true;
        QVERIFY(provider->finish(Operation::StartScan, found));
        QTRY_COMPARE(config.devicesModel().size(), 2);
        config.setDeviceByAddress("12:34:56:78:90:AC");
        QCOMPARE(config.device().coreConfigurations(), lowEnergy ? QBluetoothDeviceInfo::LowEnergyCoreConfiguration
                                                                 : QBluetoothDeviceInfo::BaseRateCoreConfiguration);
        QCOMPARE(config.selectedRssi(), -70);
        QVERIFY(!config.scanning());
        QCOMPARE(config.address(), QStringLiteral("12:34:56:78:90:AC"));
    }

    void nativePermissionFailureReachesConfiguration()
    {
        auto provider = std::make_shared<DiscoveryProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        BluetoothConfiguration config("Denied");
        QSignalSpy errors(&config, &BluetoothConfiguration::errorOccurred);
        config.startScan();
        HarmonyBluetoothProvider::Result denied;
        denied.errorCode = 201;
        denied.error = "Bluetooth access was denied";
        QVERIFY(provider->finish(Operation::StartScan, denied));
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("201"));
        QVERIFY(!config.scanning());
    }

    void nativeInvalidAdapterIsRejectedBeforeSystemManagement()
    {
        BluetoothConfiguration config("Invalid adapter");
        QSignalSpy errors(&config, &BluetoothConfiguration::errorOccurred);
        config.selectAdapter(QStringLiteral("not-an-address"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), QStringLiteral("Invalid adapter address"));
    }

    void nativePairingAndPowerReportSystemManagement()
    {
        BluetoothConfiguration config("Native");
        QSignalSpy errors(&config, &BluetoothConfiguration::errorOccurred);
        config.requestPairing("12:34:56:78:90:AB");
        config.removePairing("12:34:56:78:90:AB");
        config.powerOnAdapter();
        config.powerOffAdapter();
        config.setAdapterDiscoverable(true);
        QCOMPARE(errors.count(), 5);
        QVERIFY(!config.isPaired("12:34:56:78:90:AB"));
        QVERIFY(config.getPairingStatus("12:34:56:78:90:AB").contains("system"));
    }

    void workerFactoryUsesNativeTransportAndBinaryIo_data()
    {
        QTest::addColumn<bool>("lowEnergy");
        QTest::newRow("SPP") << false;
        QTest::newRow("BLE") << true;
    }

    void workerFactoryUsesNativeTransportAndBinaryIo()
    {
        QFETCH(bool, lowEnergy);
        auto provider = std::make_shared<DiscoveryProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        BluetoothConfiguration config("Worker");
        config.setServiceUuid("FFE0");
        config.setWriteUuid("FFE1");
        config.setReadUuid("FFE2");
        config.setMode(lowEnergy ? BluetoothConfiguration::BluetoothMode::ModeLowEnergy
                                 : BluetoothConfiguration::BluetoothMode::ModeClassic);
        config.startScan();
        HarmonyBluetoothProvider::Result found;
        found.devices = {{"12:34:56:78:90:AB", "Radio", -44}};
        QVERIFY(provider->finish(Operation::StartScan, found));
        QTRY_COMPARE(config.devicesModel().size(), 1);
        config.setDeviceByAddress("12:34:56:78:90:AB");
        std::unique_ptr<BluetoothWorker> worker(BluetoothWorker::create(&config));
        QCOMPARE(QString::fromLatin1(worker->metaObject()->className()), QStringLiteral("HarmonyBluetoothWorker"));
        QSignalSpy connected(worker.get(), &BluetoothWorker::connected);
        QSignalSpy received(worker.get(), &BluetoothWorker::dataReceived);
        QSignalSpy sent(worker.get(), &BluetoothWorker::dataSent);
        QSignalSpy disconnected(worker.get(), &BluetoothWorker::disconnected);
        worker->setupConnection();
        worker->connectLink();
        QCOMPARE(provider->history.last().options.lowEnergy, lowEnergy);
        QCOMPARE(provider->history.last().options.serviceUuid, QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(provider->history.last().options.writeUuid, QStringLiteral("0000ffe1-0000-1000-8000-00805f9b34fb"));
        QCOMPARE(provider->history.last().options.notifyUuid, QStringLiteral("0000ffe2-0000-1000-8000-00805f9b34fb"));
        QVERIFY(provider->finish(Operation::Open));
        QTRY_COMPARE(connected.count(), 1);
        QVERIFY(worker->isConnected());
        HarmonyBluetoothProvider::Result data;
        data.data = QByteArray::fromHex("fd0001ff00");
        QVERIFY(provider->finish(Operation::Read, data));
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received.first().first().toByteArray(), data.data);
        worker->writeData(data.data);
        HarmonyBluetoothProvider::Result written;
        written.written = data.data.size();
        QVERIFY(provider->finish(Operation::Write, written));
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.first().first().toByteArray(), data.data);
        worker->disconnectLink();
        QTRY_COMPARE(disconnected.count(), 1);
        QVERIFY(!worker->isConnected());
    }

    void nativeCapabilityIsTransportSpecific()
    {
        auto provider = std::make_shared<DiscoveryProvider>();
        provider->classicSupported = false;
        HarmonyBluetoothBridge::setTestProvider(provider);
        QVERIFY(HarmonyBluetoothBridge::isSupported());
        QVERIFY(!HarmonyBluetoothBridge::isTransportSupported(false));
        QVERIFY(HarmonyBluetoothBridge::isTransportSupported(true));
        BluetoothConfiguration config("Unavailable SPP");
        QSignalSpy errors(&config, &BluetoothConfiguration::errorOccurred);
        config.startScan();
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("801"));
        QVERIFY(provider->history.isEmpty());
        std::unique_ptr<BluetoothWorker> worker(BluetoothWorker::create(&config));
        QSignalSpy workerErrors(worker.get(), &BluetoothWorker::errorOccurred);
        worker->connectLink();
        QCOMPARE(workerErrors.count(), 1);
        QVERIFY(provider->history.isEmpty());
        config.setMode(BluetoothConfiguration::BluetoothMode::ModeLowEnergy);
        config.startScan();
        QCOMPARE(provider->history.last().operation, Operation::StartScan);
        QVERIFY(provider->history.last().options.lowEnergy);
    }

    void copyFromStopsOldTransportScan()
    {
        auto provider = std::make_shared<DiscoveryProvider>();
        HarmonyBluetoothBridge::setTestProvider(provider);
        BluetoothConfiguration active("Active");
        active.startScan();
        QVERIFY(active.scanning());
        BluetoothConfiguration edited("Edited");
        edited.setMode(BluetoothConfiguration::BluetoothMode::ModeLowEnergy);
        active.copyFrom(&edited);
        QVERIFY(!active.scanning());
        QCOMPARE(active.mode(), edited.mode());
        QCOMPARE(provider->history.last().operation, Operation::StopScan);
        QVERIFY(!provider->history.last().options.lowEnergy);
    }
};
QTEST_GUILESS_MAIN(ConfigurationTest)
#include "ConfigurationTest.moc"
