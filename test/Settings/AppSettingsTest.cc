#include "AppSettingsTest.h"
#include "AppSettings.h"
#include "QGCApplication.h"
#include "SettingsManager.h"
#include "MAVLinkProtocol.h"
#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
#include "BluetoothLink.h"
#include "LinkManager.h"
#include "QmlObjectListModel.h"
#endif

#include <QtCore/QDir>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QMap>
#include <QtCore/QPointer>
#include <QtCore/QSettings>
#include <QtCore/QScopedValueRollback>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
namespace {
class LinkSettingsRestore
{
public:
    LinkSettingsRestore()
    {
        QSettings settings;
        settings.beginGroup(LinkConfiguration::settingsRoot());
        for (const QString &key : settings.allKeys()) { values.insert(key, settings.value(key)); }
        settings.remove(QString());
    }
    ~LinkSettingsRestore()
    {
        QSettings settings;
        settings.beginGroup(LinkConfiguration::settingsRoot());
        settings.remove(QString());
        for (auto it = values.cbegin(); it != values.cend(); ++it) { settings.setValue(it.key(), it.value()); }
        settings.sync();
    }
private:
    QMap<QString, QVariant> values;
};
}
#endif

void AppSettingsTest::init()
{
    UnitTest::init();
    _originalSavePath = QSettings().value(AppSettings::savePathName);
    _originalGlobalSavePath = SettingsManager::instance()->appSettings()->savePath()->rawValue();
}

void AppSettingsTest::cleanup()
{
    QSettings settings;
    SettingsManager::instance()->appSettings()->savePath()->setRawValue(_originalGlobalSavePath);
    if (_originalSavePath.isValid()) {
        settings.setValue(AppSettings::savePathName, _originalSavePath);
    } else {
        settings.remove(AppSettings::savePathName);
    }
    UnitTest::cleanup();
}

void AppSettingsTest::_defaultSavePathWritable()
{
    QSettings().setValue(AppSettings::savePathName, QString());
    std::unique_ptr<AppSettings> settings;
    {
        QScopedValueRollback<bool> testMode(qgcApp()->_runningUnitTests, false);
        settings = std::make_unique<AppSettings>();
    }
    const QString expected = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(!expected.isEmpty());
    QCOMPARE(settings->savePath()->rawValue().toString(), expected);
    QCOMPARE(settings->telemetrySavePath(), QDir(expected).filePath(AppSettings::telemetryDirectory));

    QTemporaryDir outputDirectory(QDir(settings->telemetrySavePath()).filePath("app-settings-path-test-XXXXXX"));
    QVERIFY(outputDirectory.isValid());
    const QString savedPath = outputDirectory.filePath("saved.tlog");
    const QByteArray payload("telemetry-path-regression");
    QFile saved(savedPath);
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(saved.write(payload), payload.size());
    QVERIFY(saved.flush());
    saved.close();
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), payload);
    saved.close();
    QVERIFY(QFile::remove(savedPath));
}

void AppSettingsTest::_legacyDefaultMigrated()
{
    const QString legacy = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QCoreApplication::applicationName());
    QSettings().setValue(AppSettings::savePathName, legacy);
    std::unique_ptr<AppSettings> settings;
    {
        QScopedValueRollback<bool> testMode(qgcApp()->_runningUnitTests, false);
        settings = std::make_unique<AppSettings>();
    }
    const QString expected = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCOMPARE(settings->savePath()->rawValue().toString(), expected);
    QCOMPARE(QSettings().value(AppSettings::savePathName).toString(), expected);
}

void AppSettingsTest::_customSavePathPreserved()
{
    QTemporaryDir customDirectory;
    QVERIFY(customDirectory.isValid());
    const QString customPath = customDirectory.filePath("selected-folder");
    QSettings().setValue(AppSettings::savePathName, customPath);
    std::unique_ptr<AppSettings> settings;
    {
        QScopedValueRollback<bool> testMode(qgcApp()->_runningUnitTests, false);
        settings = std::make_unique<AppSettings>();
    }
    QCOMPARE(settings->savePath()->rawValue().toString(), customPath);
    QCOMPARE(QSettings().value(AppSettings::savePathName).toString(), customPath);
}

void AppSettingsTest::_telemetryMultiChunkArchived()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppSettings *const settings = SettingsManager::instance()->appSettings();
    settings->savePath()->setRawValue(directory.path());
    const QString sourcePath = directory.filePath("multi-chunk.mavlink");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QByteArray block(32768, Qt::Uninitialized);
    for (qsizetype i = 0; i < block.size(); ++i) { block[i] = char(i % 251); }
    QCryptographicHash expected(QCryptographicHash::Sha256);
    for (int i = 0; i < 129; ++i) {
        QCOMPARE(source.write(block), qint64(block.size()));
        expected.addData(block);
    }
    source.close();
    MAVLinkProtocol::instance()->_saveTelemetryLog(sourcePath);
    QVERIFY(!QFile::exists(sourcePath));
    const QDir output(settings->telemetrySavePath());
    const QStringList files = output.entryList({"*.tlog"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile saved(output.filePath(files.first()));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.size(), qint64(129) * block.size());
    QCryptographicHash actual(QCryptographicHash::Sha256);
    QVERIFY(actual.addData(&saved));
    QCOMPARE(actual.result(), expected.result());
}

void AppSettingsTest::_failedTelemetryCopyRetained()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AppSettings *const settings = SettingsManager::instance()->appSettings();
    settings->savePath()->setRawValue(temporaryDirectory.path());
    const QString telemetryPath = settings->telemetrySavePath();
    QVERIFY(QDir(telemetryPath).exists());
    const QString sourcePath = temporaryDirectory.filePath("failed-copy.tlog");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("telemetry-copy-regression"), qint64(25));
    source.close();
    QVERIFY(QFile::setPermissions(telemetryPath, QFile::ReadOwner | QFile::ExeOwner));
    MAVLinkProtocol::instance()->_saveTelemetryLog(sourcePath);
    const bool sourceRetained = QFile::exists(sourcePath);
    QVERIFY(QFile::setPermissions(telemetryPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(sourceRetained);
    MAVLinkProtocol::instance()->_saveTelemetryLog(sourcePath);
    QVERIFY(!QFile::exists(sourcePath));
    const QStringList savedFiles = QDir(telemetryPath).entryList(QStringList("*.tlog"), QDir::Files);
    QCOMPARE(savedFiles.count(), 1);
    QFile saved(QDir(telemetryPath).filePath(savedFiles.first()));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), QByteArray("telemetry-copy-regression"));
}

void AppSettingsTest::_invalidTelemetryPathRetained()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString blockedPath = temporaryDirectory.filePath("not-a-directory");
    QFile blocked(blockedPath);
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    SettingsManager::instance()->appSettings()->savePath()->setRawValue(blockedPath);
    const QString sourcePath = temporaryDirectory.filePath("invalid-path.tlog");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("telemetry-path-regression"), qint64(25));
    source.close();
    MAVLinkProtocol::instance()->_saveTelemetryLog(sourcePath);
    QVERIFY(QFile::exists(sourcePath));
}

#if defined(Q_OS_HARMONY) && defined(QGC_ENABLE_BLUETOOTH)
void AppSettingsTest::_bluetoothLinkManagerRoundTrip_data()
{
    QTest::addColumn<bool>("lowEnergy");
    QTest::addColumn<QString>("serviceInput");
    QTest::addColumn<QString>("writeInput");
    QTest::addColumn<QString>("notifyInput");
    QTest::addColumn<QString>("serviceExpected");
    QTest::addColumn<QString>("writeExpected");
    QTest::addColumn<QString>("notifyExpected");
    const QString service = QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    const QString write = QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    const QString notify = QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    QTest::newRow("SPP") << false << service << write << notify << service << write << notify;
    QTest::newRow("BLE-NUS") << true << service << write << notify << service << write << notify;
    QTest::newRow("BLE-custom") << true
        << QStringLiteral(" 12345678-1111-2222-3333-445566778899 ") << QStringLiteral("ABCD") << QStringLiteral("1234ABCD")
        << QStringLiteral("12345678-1111-2222-3333-445566778899")
        << QStringLiteral("0000abcd-0000-1000-8000-00805f9b34fb")
        << QStringLiteral("1234abcd-0000-1000-8000-00805f9b34fb");
}

void AppSettingsTest::_bluetoothLinkManagerRoundTrip()
{
    QFETCH(bool, lowEnergy);
    QFETCH(QString, serviceInput);
    QFETCH(QString, writeInput);
    QFETCH(QString, notifyInput);
    QFETCH(QString, serviceExpected);
    QFETCH(QString, writeExpected);
    QFETCH(QString, notifyExpected);
    QVERIFY(qgcApp()->runningUnitTests());
    LinkSettingsRestore restore;
    const QString settingsRoot = LinkConfiguration::settingsRoot();
    const QString selectionRoot = settingsRoot + QStringLiteral("/SelectionSeed");
    const QString address = QStringLiteral("12:34:56:78:90:AB");
    QSettings settings;
    settings.setValue(selectionRoot + "/deviceName", QStringLiteral("Configuration-only radio"));
    settings.setValue(selectionRoot + "/address", address);
    settings.setValue(selectionRoot + "/lowEnergy", lowEnergy);

    // Local real managers are never initialized, so no timers, scans or links start.
    LinkManager manager;
    std::unique_ptr<BluetoothConfiguration> pending(qobject_cast<BluetoothConfiguration *>(
        manager.createConfiguration(LinkConfiguration::TypeBluetooth, QStringLiteral("Bluetooth before edit"))));
    QVERIFY(pending);
    pending->loadSettings(settings, selectionRoot);
    pending->setBleServiceUuid(QStringLiteral("ffe0"));
    pending->setBleWriteUuid(QStringLiteral("ffe1"));
    pending->setBleNotifyUuid(QStringLiteral("ffe1"));
    pending->setAutoConnect(true);
    pending->setHighLatency(true);
    BluetoothConfiguration *const original = pending.get();
    manager.endCreateConfiguration(pending.release());
    auto *const model = manager.property("linkConfigurations").value<QmlObjectListModel *>();
    QVERIFY(model);
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->get(0), original);
    QCOMPARE(settings.value(settingsRoot + "/count").toInt(), 1);
    QVERIFY(settings.value(settingsRoot + "/Link0/auto").toBool());

    std::unique_ptr<BluetoothConfiguration> edited(qobject_cast<BluetoothConfiguration *>(manager.startConfigurationEditing(original)));
    QVERIFY(edited);
    QVERIFY(edited.get() != original);
    QCOMPARE(edited->lowEnergy(), lowEnergy);
    QCOMPARE(edited->address(), address);
    QCOMPARE(edited->bleServiceUuid(), QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb"));
    edited->setName(QStringLiteral("Bluetooth after edit"));
    edited->setBleServiceUuid(serviceInput);
    edited->setBleWriteUuid(writeInput);
    edited->setBleNotifyUuid(notifyInput);
    edited->setAutoConnect(false);
    QCOMPARE(original->name(), QStringLiteral("Bluetooth before edit"));
    QVERIFY(original->isAutoConnect());
    QCOMPARE(original->bleServiceUuid(), QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb"));
    QPointer<BluetoothConfiguration> editingPointer(edited.get());
    manager.endConfigurationEditing(original, edited.release());
    QVERIFY(editingPointer.isNull());
    QCOMPARE(model->count(), 1);
    QCOMPARE(original->name(), QStringLiteral("Bluetooth after edit"));
    QCOMPARE(original->bleServiceUuid(), serviceExpected);
    QCOMPARE(original->bleWriteUuid(), writeExpected);
    QCOMPARE(original->bleNotifyUuid(), notifyExpected);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
    QFile savedIni(settings.fileName());
    QVERIFY(savedIni.open(QIODevice::ReadOnly));
    const QByteArray savedBytes = savedIni.readAll();
    savedIni.close();
    QVERIFY(savedBytes.contains("Bluetooth after edit"));
    QVERIFY(savedBytes.contains(serviceExpected.toUtf8()));

    LinkManager reloaded;
    reloaded.loadLinkConfigurationList();
    auto *const restoredModel = reloaded.property("linkConfigurations").value<QmlObjectListModel *>();
    QVERIFY(restoredModel);
    QCOMPARE(restoredModel->count(), 1);
    auto *const restored = qobject_cast<BluetoothConfiguration *>(restoredModel->get(0));
    QVERIFY(restored);
    QCOMPARE(restored->name(), QStringLiteral("Bluetooth after edit"));
    QCOMPARE(restored->deviceName(), QStringLiteral("Configuration-only radio"));
    QCOMPARE(restored->address(), address);
    QCOMPARE(restored->lowEnergy(), lowEnergy);
    QCOMPARE(restored->bleServiceUuid(), serviceExpected);
    QCOMPARE(restored->bleWriteUuid(), writeExpected);
    QCOMPARE(restored->bleNotifyUuid(), notifyExpected);
    QVERIFY(!restored->isAutoConnect());
    QVERIFY(restored->isHighLatency());
    QVERIFY(manager.links().isEmpty());
    QVERIFY(reloaded.links().isEmpty());
}
#endif
