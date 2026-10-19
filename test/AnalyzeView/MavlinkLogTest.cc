#include "MavlinkLogTest.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QScopedValueRollback>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>

#include "AppSettings.h"
#include "LinkManager.h"
#include "MAVLinkLib.h"
#include "MAVLinkProtocol.h"
#include "MavlinkSettings.h"
#include "MultiVehicleManager.h"
#include "SettingsManager.h"
#include "Vehicle.h"

namespace {
int tempMavlinkLogCount(const char* extension)
{
    QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    const QStringList logFiles(tmpDir.entryList(QStringList(QStringLiteral("*.%1").arg(extension)), QDir::Files));
    return logFiles.count();
}

QStringList savedTelemetryLogFiles()
{
    const QString telemetrySavePath = SettingsManager::instance()->appSettings()->telemetrySavePath();
    const QDir saveDir(telemetrySavePath);
    const QString filter = QStringLiteral("*.%1").arg(AppSettings::telemetryFileExtension);
    return saveDir.entryList(QStringList(filter), QDir::Files);
}

int savedTelemetryLogCount()
{
    return savedTelemetryLogFiles().count();
}

void removeSavedTelemetryLogs()
{
    const QString telemetrySavePath = SettingsManager::instance()->appSettings()->telemetrySavePath();
    QDir saveDir(telemetrySavePath);
    const QString filter = QStringLiteral("*.%1").arg(AppSettings::telemetryFileExtension);
    const QStringList logFiles = saveDir.entryList(QStringList(filter), QDir::Files);
    for (const QString& file : logFiles) {
        QVERIFY(saveDir.remove(file));
    }
}

void verifyArmedTelemetryLog(const QString& filePath, int vehicleId, quint64 startTimeUSecs, quint64 endTimeUSecs)
{
    QFile file(filePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray bytes = file.readAll();
    QVERIFY(!bytes.isEmpty());

    qsizetype offset = 0;
    quint64 previousTimestamp = 0;
    bool sawUnarmedHeartbeat = false;
    bool sawArmedHeartbeat = false;
    while (offset < bytes.size()) {
        QVERIFY(bytes.size() - offset >= static_cast<qsizetype>(sizeof(quint64)));
        const quint64 timestamp = qFromBigEndian<quint64>(bytes.constData() + offset);
        QVERIFY(timestamp >= startTimeUSecs);
        QVERIFY(timestamp <= endTimeUSecs);
        QVERIFY(timestamp >= previousTimestamp);
        QCOMPARE(timestamp % 1000, quint64(0));
        previousTimestamp = timestamp;
        offset += sizeof(quint64);

        QVERIFY(bytes.size() - offset >= 3);
        const uint8_t magic = static_cast<uint8_t>(bytes.at(offset));
        QVERIFY(magic == MAVLINK_STX || magic == MAVLINK_STX_MAVLINK1);
        const uint8_t payloadLength = static_cast<uint8_t>(bytes.at(offset + 1));
        const qsizetype headerLength =
            magic == MAVLINK_STX ? MAVLINK_CORE_HEADER_LEN : MAVLINK_CORE_HEADER_MAVLINK1_LEN;
        qsizetype frameLength = headerLength + 1 + payloadLength + MAVLINK_NUM_CHECKSUM_BYTES;
        if (magic == MAVLINK_STX && (static_cast<uint8_t>(bytes.at(offset + 2)) & MAVLINK_IFLAG_SIGNED)) {
            frameLength += MAVLINK_SIGNATURE_BLOCK_LEN;
        }
        QVERIFY(bytes.size() - offset >= frameLength);

        mavlink_message_t parseMessage{};
        mavlink_status_t parseStatus{};
        mavlink_message_t message{};
        mavlink_status_t messageStatus{};
        for (qsizetype i = 0; i < frameLength; ++i) {
            const uint8_t framing = mavlink_frame_char_buffer(
                &parseMessage, &parseStatus, static_cast<uint8_t>(bytes.at(offset + i)), &message, &messageStatus);
            const uint8_t expectedFraming = i + 1 == frameLength ? MAVLINK_FRAMING_OK : MAVLINK_FRAMING_INCOMPLETE;
            QCOMPARE(framing, expectedFraming);
        }
        offset += frameLength;

        if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT && message.sysid == vehicleId &&
            message.compid == MAV_COMP_ID_AUTOPILOT1) {
            const bool armed = mavlink_msg_heartbeat_get_base_mode(&message) & MAV_MODE_FLAG_SAFETY_ARMED;
            sawUnarmedHeartbeat |= !armed;
            sawArmedHeartbeat |= armed;
        }
    }
    QCOMPARE(offset, bytes.size());
    QVERIFY(sawUnarmedHeartbeat);
    QVERIFY(sawArmedHeartbeat);
}
}  // namespace

void MavlinkLogTest::init()
{
    UnitTest::init();
    _settingsOverridden = false;
    MAVLinkProtocol::instance()->init();
    MultiVehicleManager::instance()->init();
    LinkManager::instance()->setConnectionsAllowed();
    // Make sure temp directory is clear of mavlink logs
    QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    QStringList logFiles(tmpDir.entryList(QStringList(QString("*.%1").arg(_logFileExtension)), QDir::Files));
    for (const QString& logFile : logFiles) {
        QVERIFY(tmpDir.remove(logFile));
    }

    const QString testRoot = QDir::tempPath() + QStringLiteral("/QGroundControl_unittest_MavlinkLogTest");
    QVERIFY(QDir().mkpath(testRoot));
    _originalSavePath = SettingsManager::instance()->appSettings()->savePath()->rawValue();
    _originalDisableAllPersistence = SettingsManager::instance()->appSettings()->disableAllPersistence()->rawValue();
    _originalTelemetrySave = SettingsManager::instance()->mavlinkSettings()->telemetrySave()->rawValue();
    _originalTelemetrySaveNotArmed =
        SettingsManager::instance()->mavlinkSettings()->telemetrySaveNotArmed()->rawValue();
    _settingsOverridden = true;
    SettingsManager::instance()->appSettings()->savePath()->setRawValue(testRoot);
    SettingsManager::instance()->appSettings()->disableAllPersistence()->setRawValue(false);
    SettingsManager::instance()->mavlinkSettings()->telemetrySave()->setRawValue(true);
    SettingsManager::instance()->mavlinkSettings()->telemetrySaveNotArmed()->setRawValue(false);

    const QString telemetrySavePath = SettingsManager::instance()->appSettings()->telemetrySavePath();
    QVERIFY(!telemetrySavePath.isEmpty());
    QVERIFY(QDir().mkpath(telemetrySavePath));
    removeSavedTelemetryLogs();
}

void MavlinkLogTest::cleanup()
{
    const auto restoreSettings = qScopeGuard([this]() {
        if (_settingsOverridden) {
            SettingsManager::instance()->appSettings()->savePath()->setRawValue(_originalSavePath);
            SettingsManager::instance()->appSettings()->disableAllPersistence()->setRawValue(
                _originalDisableAllPersistence);
            SettingsManager::instance()->mavlinkSettings()->telemetrySave()->setRawValue(_originalTelemetrySave);
            SettingsManager::instance()->mavlinkSettings()->telemetrySaveNotArmed()->setRawValue(
                _originalTelemetrySaveNotArmed);
            _settingsOverridden = false;
        }
        UnitTest::cleanup();
    });
    _disconnectMockLink();
    // Make sure no left over logs in temp directory
    QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    QStringList logFiles(tmpDir.entryList(QStringList(QString("*.%1").arg(_logFileExtension)), QDir::Files));
    if (_settingsOverridden) {
        removeSavedTelemetryLogs();
    }
    QCOMPARE(logFiles.count(), 0);
}

void MavlinkLogTest::_createTempLogFile(bool zeroLength)
{
    const QString tempDirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString fileName = QStringLiteral("%1_%2.%3")
                                 .arg(_tempLogFileTemplate)
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                 .arg(_logFileExtension);
    QFile tempLogFile(QDir(tempDirPath).filePath(fileName));
    QVERIFY(tempLogFile.open(QIODevice::WriteOnly));
    if (!zeroLength) {
        tempLogFile.write("foo");
    }
    tempLogFile.close();
}

void MavlinkLogTest::_bootLogDetectionCancel_test()
{
    // Create a fake mavlink log
    _createTempLogFile(false);
    QCOMPARE(tempMavlinkLogCount(_logFileExtension), 1);
    MAVLinkProtocol::instance()->checkForLostLogFiles();
    QVERIFY_TRUE_WAIT(tempMavlinkLogCount(_logFileExtension) == 0, TestTimeout::mediumMs());
}

void MavlinkLogTest::_bootLogDetectionSave_test()
{
    // Create a fake mavlink log
    _createTempLogFile(false);
    QCOMPARE(tempMavlinkLogCount(_logFileExtension), 1);
    const int initialSavedLogCount = savedTelemetryLogCount();
    MAVLinkProtocol::instance()->checkForLostLogFiles();
    QVERIFY_TRUE_WAIT(tempMavlinkLogCount(_logFileExtension) == 0, TestTimeout::mediumMs());
    QVERIFY_TRUE_WAIT(savedTelemetryLogCount() == (initialSavedLogCount + 1), TestTimeout::longMs());
    removeSavedTelemetryLogs();
}

void MavlinkLogTest::_bootLogDetectionZeroLength_test()
{
    // Create a fake empty mavlink log
    _createTempLogFile(true);
    QCOMPARE(tempMavlinkLogCount(_logFileExtension), 1);
    MAVLinkProtocol::instance()->checkForLostLogFiles();
    QVERIFY_TRUE_WAIT(tempMavlinkLogCount(_logFileExtension) == 0, TestTimeout::mediumMs());
    // Zero length log files should not generate any additional UI pop-ups. It should just be deleted silently.
}

void MavlinkLogTest::_connectLogWorker(bool arm)
{
    MAVLinkProtocol* const protocol = MAVLinkProtocol::instance();
    QVERIFY(!protocol->_loggingEnabledForUnitTests);
    QVERIFY(!protocol->_tempLogFile->isOpen());
    QVERIFY(!protocol->_vehicleWasArmed);
    const QScopedValueRollback<bool> loggingEnabled(protocol->_loggingEnabledForUnitTests, true);
    const QStringList initialSavedLogFiles = savedTelemetryLogFiles();
    const int initialSavedLogCount = initialSavedLogFiles.count();
    QVERIFY(!SettingsManager::instance()->mavlinkSettings()->telemetrySaveNotArmed()->rawValue().toBool());
    const quint64 startTimeUSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    _connectMockLink();
    Vehicle* const connectedVehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(connectedVehicle);
    QTRY_VERIFY_WITH_TIMEOUT(protocol->_tempLogFile->isOpen(), TestTimeout::mediumMs());
    const QString tempLogFilePath = protocol->_tempLogFile->fileName();
    QVERIFY(QFile::exists(tempLogFilePath));
    QCOMPARE(tempMavlinkLogCount(_logFileExtension), 1);
    const int vehicleId = connectedVehicle->id();
    bool unarmedHeartbeatLogged = false;
    const auto heartbeatConnection = connect(
        protocol, &MAVLinkProtocol::messageReceived, this, [&](LinkInterface* link, const mavlink_message_t& message) {
            if (link == _mockLink && message.msgid == MAVLINK_MSG_ID_HEARTBEAT && message.sysid == vehicleId &&
                message.compid == MAV_COMP_ID_AUTOPILOT1 &&
                !(mavlink_msg_heartbeat_get_base_mode(&message) & MAV_MODE_FLAG_SAFETY_ARMED)) {
                unarmedHeartbeatLogged = true;
            }
        });
    QVERIFY(heartbeatConnection);
    const auto disconnectHeartbeat = qScopeGuard([heartbeatConnection]() { QObject::disconnect(heartbeatConnection); });
    QTRY_VERIFY_WITH_TIMEOUT(unarmedHeartbeatLogged, TestTimeout::mediumMs());
    QTRY_VERIFY_WITH_TIMEOUT(protocol->_tempLogFile->size() > 8, TestTimeout::mediumMs());
    QVERIFY(protocol->_tempLogFile->flush());
    QVERIFY(!connectedVehicle->armed());
    if (arm) {
        connectedVehicle->setArmedShowError(true);
        QTRY_VERIFY_WITH_TIMEOUT(connectedVehicle->armed(), TestTimeout::mediumMs());
    }
    QCOMPARE(protocol->_vehicleWasArmed, arm);
    _disconnectMockLink();
    QTRY_VERIFY_WITH_TIMEOUT(!protocol->_tempLogFile->isOpen(), TestTimeout::mediumMs());
    QVERIFY(!QFile::exists(tempLogFilePath));
    const quint64 endTimeUSecs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    if (arm) {
        QTRY_COMPARE_WITH_TIMEOUT(savedTelemetryLogCount(), initialSavedLogCount + 1, TestTimeout::mediumMs());
        QStringList newLogFiles = savedTelemetryLogFiles();
        for (const QString& file : initialSavedLogFiles) {
            newLogFiles.removeAll(file);
        }
        QCOMPARE(newLogFiles.count(), 1);
        const QDir saveDir(SettingsManager::instance()->appSettings()->telemetrySavePath());
        verifyArmedTelemetryLog(saveDir.absoluteFilePath(newLogFiles.first()), vehicleId, startTimeUSecs, endTimeUSecs);
    } else {
        QCOMPARE(savedTelemetryLogCount(), initialSavedLogCount);
    }
    QCOMPARE(tempMavlinkLogCount(_logFileExtension), 0);
}

void MavlinkLogTest::_connectLogNoArm_test()
{
    _connectLogWorker(false);
}

void MavlinkLogTest::_connectLogArm_test()
{
    _connectLogWorker(true);
}

void MavlinkLogTest::_deleteTempLogFiles_test()
{
    // Verify that the MAVLinkProtocol::deleteTempLogFiles api works correctly
    _createTempLogFile(false);
    MAVLinkProtocol::deleteTempLogFiles();
    QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    QStringList logFiles(tmpDir.entryList(QStringList(QString("*.%1").arg(_logFileExtension)), QDir::Files));
    QCOMPARE(logFiles.count(), 0);
}

UT_REGISTER_TEST(MavlinkLogTest, TestLabel::Integration, TestLabel::AnalyzeView, TestLabel::Vehicle, TestLabel::Serial)
