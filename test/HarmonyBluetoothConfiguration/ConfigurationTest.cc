#include "Configuration.h"
#include <QtCore/QMetaProperty>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <algorithm>

using Operation = HarmonyBluetoothProvider::Operation;

class DiscoveryProvider final : public HarmonyBluetoothProvider
{
public:
    QList<Request> history;
    QList<Completion> starts;
    QList<Request> pending;
    QList<Completion> completions;
    bool supported(QString *, bool *ready) override { if (ready) { *ready = true; } return true; }
    void call(const Request &request, Completion completion) override
    {
        history.append(request);
        if (request.operation == Operation::StartScan) { starts.append(std::move(completion)); }
        else if (request.operation == Operation::StopScan || request.operation == Operation::Close) { completion({}); }
        else { pending.append(request); completions.append(std::move(completion)); }
    }
    void found(const QList<HarmonyBluetoothDeviceInfo> &devices, bool scanning = false)
    {
        if (starts.isEmpty()) { qFatal("Expected discovery request is missing"); }
        Result result;
        result.devices = devices;
        result.scanning = scanning;
        starts.takeFirst()(result);
    }
    int count(Operation operation) const
    {
        return std::count_if(history.cbegin(), history.cend(),
            [operation](const Request &request) { return request.operation == operation; });
    }
    Request last(Operation operation) const
    {
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            if (it->operation == operation) { return *it; }
        }
        return {};
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

namespace {
const QString firstAddress = QStringLiteral("12:34:56:78:90:AB");
const QString secondAddress = QStringLiteral("12:34:56:78:90:AC");
const QString soloAddress = QStringLiteral("12:34:56:78:90:AD");
const QString serviceUuid = QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb");
const QString writeUuid = QStringLiteral("0000ffe1-0000-1000-8000-00805f9b34fb");
const QString notifyUuid = QStringLiteral("0000ffe2-0000-1000-8000-00805f9b34fb");

void customBle(BluetoothConfiguration &config)
{
    config.setLowEnergy(true);
    config.setBleServiceUuid(QStringLiteral(" FFE0 "));
    config.setBleWriteUuid(QStringLiteral("FFE1"));
    config.setBleNotifyUuid(QStringLiteral("FFE2"));
}
}

class ConfigurationTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void defaultPropertiesExposeClassicAndNus();
    void optionPropertyChangesNotifyObservers();
    void uuidNormalization_data();
    void uuidNormalization();
    void copyConstructorPreservesBleOptions();
    void copyFromPreservesBleOptionsAndSelectedDevice();
    void settingsRoundTripPreservesBleOptionsAndSelectedDevice();
    void legacySettingsKeepClassicDefaults();
    void modeSwitchStopsScanAndClearsSelection_data();
    void modeSwitchStopsScanAndClearsSelection();
    void settingSameModePreservesSelectionAndScan();
    void duplicateNamesSelectExactAddress();
    void ambiguousRawNameDoesNotSelectFirstDevice();
    void uniqueRawNameRemainsCompatible();
    void scanReceivesConfiguredTransportAndUuids();
    void workerOptionsAndBinaryIo_data();
    void workerOptionsAndBinaryIo();
    void workerEmptyAddressMakesNoOpenRequest();
    void workerReadErrorForwardsFailureAndDisconnects();
private:
    std::shared_ptr<DiscoveryProvider> provider;
};

void ConfigurationTest::init()
{
    provider = std::make_shared<DiscoveryProvider>();
    HarmonyBluetoothBridge::setTestProvider(provider);
}

void ConfigurationTest::cleanup()
{
    HarmonyBluetoothBridge::setTestProvider({});
    provider.reset();
}

void ConfigurationTest::defaultPropertiesExposeClassicAndNus()
{
    BluetoothConfiguration config(QStringLiteral("Bluetooth"));
    QVERIFY(config.property("supportsLowEnergy").toBool());
    QVERIFY(!config.property("lowEnergy").toBool());
    QCOMPARE(config.property("bleServiceUuid").toString(), QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(config.property("bleWriteUuid").toString(), QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(config.property("bleNotifyUuid").toString(), QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e"));
    const auto options = config.harmonyOptions();
    QVERIFY(!options.lowEnergy);
    QCOMPARE(options.serviceUuid, QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(options.writeUuid, QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(options.notifyUuid, QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e"));
}

void ConfigurationTest::uuidNormalization_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("trim-case-128") << QStringLiteral(" 6E400001-B5A3-F393-E0A9-E50E24DCCA9E ")
        << QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    QTest::newRow("braces") << QStringLiteral("{6E400001-B5A3-F393-E0A9-E50E24DCCA9E}")
        << QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    QTest::newRow("16-bit") << QStringLiteral(" FFE0 ") << serviceUuid;
    QTest::newRow("32-bit") << QStringLiteral("1234ABCD") << QStringLiteral("1234abcd-0000-1000-8000-00805f9b34fb");
}

void ConfigurationTest::optionPropertyChangesNotifyObservers()
{
    BluetoothConfiguration config(QStringLiteral("Properties"));
    QSignalSpy changed(&config, &BluetoothConfiguration::bluetoothOptionsChanged);
    QVERIFY(config.setProperty("lowEnergy", true));
    QVERIFY(config.setProperty("bleServiceUuid", QStringLiteral(" FFE0 ")));
    QVERIFY(config.setProperty("bleWriteUuid", QStringLiteral("FFE1")));
    QVERIFY(config.setProperty("bleNotifyUuid", QStringLiteral("FFE2")));
    QCOMPARE(changed.count(), 4);
    QVERIFY(config.setProperty("lowEnergy", true));
    QVERIFY(config.setProperty("bleServiceUuid", serviceUuid));
    QVERIFY(config.setProperty("bleWriteUuid", writeUuid));
    QVERIFY(config.setProperty("bleNotifyUuid", notifyUuid));
    QCOMPARE(changed.count(), 4);
}

void ConfigurationTest::uuidNormalization()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    BluetoothConfiguration config(QStringLiteral("Bluetooth"));
    QVERIFY(config.setProperty("bleServiceUuid", input));
    QVERIFY(config.setProperty("bleWriteUuid", input));
    QVERIFY(config.setProperty("bleNotifyUuid", input));
    QCOMPARE(config.bleServiceUuid(), expected);
    QCOMPARE(config.bleWriteUuid(), expected);
    QCOMPARE(config.bleNotifyUuid(), expected);
    const auto options = config.harmonyOptions();
    QCOMPARE(options.serviceUuid, expected);
    QCOMPARE(options.writeUuid, expected);
    QCOMPARE(options.notifyUuid, expected);
}

void ConfigurationTest::copyConstructorPreservesBleOptions()
{
    BluetoothConfiguration source(QStringLiteral("Source"));
    customBle(source);
    BluetoothConfiguration copy(&source);
    QVERIFY(copy.lowEnergy());
    QCOMPARE(copy.bleServiceUuid(), serviceUuid);
    QCOMPARE(copy.bleWriteUuid(), writeUuid);
    QCOMPARE(copy.bleNotifyUuid(), notifyUuid);
}

void ConfigurationTest::copyFromPreservesBleOptionsAndSelectedDevice()
{
    BluetoothConfiguration source(QStringLiteral("Source"));
    customBle(source);
    source.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}});
    QTRY_COMPARE(source.nameList().size(), 1);
    source.setDevice(QStringLiteral("Radio"));
    QCOMPARE(source.address(), firstAddress);
    BluetoothConfiguration destination(QStringLiteral("Destination"));
    destination.copyFrom(&source);
    QVERIFY(destination.lowEnergy());
    QCOMPARE(destination.bleServiceUuid(), serviceUuid);
    QCOMPARE(destination.bleWriteUuid(), writeUuid);
    QCOMPARE(destination.bleNotifyUuid(), notifyUuid);
    QCOMPARE(destination.address(), firstAddress);
    QCOMPARE(destination.deviceName(), QStringLiteral("Radio"));
}

void ConfigurationTest::settingsRoundTripPreservesBleOptionsAndSelectedDevice()
{
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/settings-XXXXXX"));
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("links.ini"));
    {
        QSettings settings(path, QSettings::IniFormat);
        BluetoothConfiguration source(QStringLiteral("Source"));
        customBle(source);
        source.startScan();
        provider->found({{firstAddress, QStringLiteral("Radio")}});
        QTRY_COMPARE(source.nameList().size(), 1);
        source.setDevice(QStringLiteral("Radio"));
        source.saveSettings(settings, QStringLiteral("Link"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    QSettings settings(path, QSettings::IniFormat);
    BluetoothConfiguration restored(QStringLiteral("Restored"));
    restored.loadSettings(settings, QStringLiteral("Link"));
    QVERIFY(restored.lowEnergy());
    QCOMPARE(restored.bleServiceUuid(), serviceUuid);
    QCOMPARE(restored.bleWriteUuid(), writeUuid);
    QCOMPARE(restored.bleNotifyUuid(), notifyUuid);
    QCOMPARE(restored.address(), firstAddress);
    QCOMPARE(restored.deviceName(), QStringLiteral("Radio"));
}

void ConfigurationTest::legacySettingsKeepClassicDefaults()
{
    QTemporaryDir directory(QStringLiteral(TEST_BINARY_DIR "/legacy-XXXXXX"));
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("links.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Link/deviceName"), QStringLiteral("Old Radio"));
    settings.setValue(QStringLiteral("Link/address"), secondAddress);
    BluetoothConfiguration config(QStringLiteral("Legacy"));
    config.loadSettings(settings, QStringLiteral("Link"));
    QVERIFY(!config.lowEnergy());
    QCOMPARE(config.bleServiceUuid(), QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(config.bleWriteUuid(), QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(config.bleNotifyUuid(), QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e"));
    QCOMPARE(config.deviceName(), QStringLiteral("Old Radio"));
    QCOMPARE(config.address(), secondAddress);
}

void ConfigurationTest::modeSwitchStopsScanAndClearsSelection_data()
{
    QTest::addColumn<bool>("initialMode");
    QTest::newRow("classic-to-ble") << false;
    QTest::newRow("ble-to-classic") << true;
}

void ConfigurationTest::modeSwitchStopsScanAndClearsSelection()
{
    QFETCH(bool, initialMode);
    BluetoothConfiguration config(QStringLiteral("Switch"));
    config.setLowEnergy(initialMode);
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}}, true);
    QTRY_COMPARE(config.nameList().size(), 1);
    config.setDevice(QStringLiteral("Radio"));
    QCOMPARE(config.address(), firstAddress);
    QVERIFY(config.scanning());
    const int previousStops = provider->count(Operation::StopScan);
    config.setLowEnergy(!initialMode);
    QVERIFY(!config.scanning());
    QVERIFY(config.deviceName().isEmpty());
    QVERIFY(config.device().address.isNull());
    QVERIFY(config.nameList().isEmpty());
    QCOMPARE(provider->count(Operation::StopScan), previousStops + 1);
    QCOMPARE(provider->history.last().options.lowEnergy, initialMode);
}

void ConfigurationTest::settingSameModePreservesSelectionAndScan()
{
    BluetoothConfiguration config(QStringLiteral("Unchanged"));
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}}, true);
    QTRY_COMPARE(config.nameList().size(), 1);
    config.setDevice(QStringLiteral("Radio"));
    config.setLowEnergy(false);
    QVERIFY(config.scanning());
    QCOMPARE(config.address(), firstAddress);
    QCOMPARE(config.nameList().size(), 1);
    QCOMPARE(provider->count(Operation::StopScan), 0);
}

void ConfigurationTest::duplicateNamesSelectExactAddress()
{
    BluetoothConfiguration config(QStringLiteral("Duplicate"));
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}, {secondAddress, QStringLiteral("Radio")}});
    QTRY_COMPARE(config.nameList().size(), 2);
    const QString firstLabel = QStringLiteral("Radio (%1)").arg(firstAddress);
    const QString secondLabel = QStringLiteral("Radio (%1)").arg(secondAddress);
    QVERIFY(config.nameList().contains(firstLabel));
    QVERIFY(config.nameList().contains(secondLabel));
    config.setDevice(secondLabel);
    QCOMPARE(config.address(), secondAddress);
    config.setDevice(firstLabel);
    QCOMPARE(config.address(), firstAddress);
}

void ConfigurationTest::ambiguousRawNameDoesNotSelectFirstDevice()
{
    BluetoothConfiguration config(QStringLiteral("Ambiguous"));
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}, {secondAddress, QStringLiteral("Radio")},
        {soloAddress, QStringLiteral("Solo")}});
    QTRY_COMPARE(config.nameList().size(), 3);
    config.setDevice(QStringLiteral("Solo"));
    QCOMPARE(config.address(), soloAddress);
    config.setDevice(QStringLiteral("Radio"));
    QCOMPARE(config.address(), soloAddress);
}

void ConfigurationTest::uniqueRawNameRemainsCompatible()
{
    BluetoothConfiguration config(QStringLiteral("Unique"));
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}});
    QTRY_COMPARE(config.nameList().size(), 1);
    config.setDevice(QStringLiteral("Radio"));
    QCOMPARE(config.address(), firstAddress);
    QCOMPARE(config.deviceName(), QStringLiteral("Radio"));
}

void ConfigurationTest::scanReceivesConfiguredTransportAndUuids()
{
    BluetoothConfiguration config(QStringLiteral("Options"));
    customBle(config);
    config.startScan();
    QVERIFY(config.scanning());
    QVERIFY(!provider->history.isEmpty());
    const auto request = provider->history.last();
    QCOMPARE(request.operation, Operation::StartScan);
    QVERIFY(request.options.lowEnergy);
    QCOMPARE(request.options.serviceUuid, serviceUuid);
    QCOMPARE(request.options.writeUuid, writeUuid);
    QCOMPARE(request.options.notifyUuid, notifyUuid);
}

void ConfigurationTest::workerOptionsAndBinaryIo_data()
{
    QTest::addColumn<bool>("lowEnergy");
    QTest::newRow("spp") << false;
    QTest::newRow("ble") << true;
}

void ConfigurationTest::workerOptionsAndBinaryIo()
{
    QFETCH(bool, lowEnergy);
    BluetoothConfiguration config(QStringLiteral("Worker"));
    customBle(config);
    config.setLowEnergy(lowEnergy);
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}});
    QTRY_COMPARE(config.nameList().size(), 1);
    config.setDevice(QStringLiteral("Radio"));
    BluetoothWorker worker(&config);
    QSignalSpy connected(&worker, &BluetoothWorker::connected);
    QSignalSpy disconnected(&worker, &BluetoothWorker::disconnected);
    QSignalSpy received(&worker, &BluetoothWorker::dataReceived);
    QSignalSpy sent(&worker, &BluetoothWorker::dataSent);
    QSignalSpy errors(&worker, &BluetoothWorker::errorOccurred);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(!worker.isConnected());
    QCOMPARE(provider->count(Operation::Open), 1);
    const auto opening = provider->last(Operation::Open);
    QCOMPARE(opening.address, firstAddress);
    QCOMPARE(opening.options.lowEnergy, lowEnergy);
    QCOMPARE(opening.options.serviceUuid, serviceUuid);
    QCOMPARE(opening.options.writeUuid, writeUuid);
    QCOMPARE(opening.options.notifyUuid, notifyUuid);
    QVERIFY(provider->finish(Operation::Open));
    QTRY_COMPARE(connected.count(), 1);
    QVERIFY(worker.isConnected());

    HarmonyBluetoothProvider::Result incoming;
    incoming.data = QByteArray::fromHex("fd0001feff00");
    QVERIFY(provider->finish(Operation::Read, incoming));
    QTRY_COMPARE(received.count(), 1);
    QCOMPARE(received.first().at(0).toByteArray(), incoming.data);
    const QByteArray outgoing = QByteArray::fromHex("fe00fd0100ff");
    worker.writeData(outgoing);
    QCOMPARE(provider->count(Operation::Write), 1);
    QCOMPARE(provider->last(Operation::Write).data, outgoing);
    HarmonyBluetoothProvider::Result written;
    written.written = outgoing.size();
    QVERIFY(provider->finish(Operation::Write, written));
    QTRY_COMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toByteArray(), outgoing);
    worker.disconnectLink();
    QVERIFY(!worker.isConnected());
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(provider->count(Operation::Close), 1);
    QCOMPARE(provider->last(Operation::Close).options.lowEnergy, lowEnergy);
    QCOMPARE(errors.count(), 0);
}

void ConfigurationTest::workerEmptyAddressMakesNoOpenRequest()
{
    BluetoothConfiguration config(QStringLiteral("No selection"));
    BluetoothWorker worker(&config);
    QSignalSpy errors(&worker, &BluetoothWorker::errorOccurred);
    QSignalSpy connected(&worker, &BluetoothWorker::connected);
    worker.setupSocket();
    worker.connectLink();
    QCOMPARE(errors.count(), 1);
    QCOMPARE(connected.count(), 0);
    QCOMPARE(provider->count(Operation::Open), 0);
    QVERIFY(!worker.isConnected());
}

void ConfigurationTest::workerReadErrorForwardsFailureAndDisconnects()
{
    BluetoothConfiguration config(QStringLiteral("Failure"));
    customBle(config);
    config.startScan();
    provider->found({{firstAddress, QStringLiteral("Radio")}});
    QTRY_COMPARE(config.nameList().size(), 1);
    config.setDevice(QStringLiteral("Radio"));
    BluetoothWorker worker(&config);
    QSignalSpy errors(&worker, &BluetoothWorker::errorOccurred);
    QSignalSpy disconnected(&worker, &BluetoothWorker::disconnected);
    QSignalSpy received(&worker, &BluetoothWorker::dataReceived);
    worker.setupSocket();
    worker.connectLink();
    QVERIFY(provider->finish(Operation::Open));
    QTRY_VERIFY(worker.isConnected());
    HarmonyBluetoothProvider::Result failed;
    failed.errorCode = 2900001;
    failed.error = QStringLiteral("test-disconnect");
    QVERIFY(provider->finish(Operation::Read, failed));
    QTRY_COMPARE(errors.count(), 1);
    QTRY_COMPARE(disconnected.count(), 1);
    QVERIFY(errors.first().at(0).toString().contains(QStringLiteral("test-disconnect")));
    QVERIFY(!worker.isConnected());
    QCOMPARE(received.count(), 0);
    QCOMPARE(provider->count(Operation::Close), 1);
}

QTEST_GUILESS_MAIN(ConfigurationTest)
#include "ConfigurationTest.moc"
