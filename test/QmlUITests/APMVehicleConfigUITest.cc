#include "APMVehicleConfigUITest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "MAVLinkProtocol.h"
#include "MockLink.h"
#include "ParameterManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(APMVehicleConfigUITest, TestLabel::Integration)

void APMVehicleConfigUITest::init()
{
    if (!apmFirmwareSupported()) {
        QSKIP("ArduPilot support not registered in this build");
    }
    VehicleConfigUITestBase::init();
    if (qEnvironmentVariableIsSet("QGC_TEST_MAV_ACK_TRACE")) {
        ignoreLogMessage("default", QtInfoMsg, QRegularExpression(QStringLiteral("^MAV_ACK_TRACE ")));
    }
}

// ---------------------------------------------------------------------------
// Shared implementation
// ---------------------------------------------------------------------------

void APMVehicleConfigUITest::_runNavigateVehicleConfig(const std::function<MockLink*()>& factory,
                                                       const QString& vehicleName)
{
    runWithMockLink(factory, [&](QPointer<MockLink> mockLink, Vehicle* vehicle) {
        const auto traceClock = std::make_shared<QElapsedTimer>();
        traceClock->start();
        QTimer traceTimer;
        QObject traceContext;
        if (qEnvironmentVariableIsSet("QGC_TEST_MAV_ACK_TRACE")) {
            connect(
                mockLink, &LinkInterface::bytesReceived, &traceContext,
                [traceClock](LinkInterface*, const QByteArray& bytes) {
                    mavlink_message_t buffer{}, message{};
                    mavlink_status_t status{}, receivedStatus{};
                    for (const char byte : bytes) {
                        if (mavlink_frame_char_buffer(&buffer, &status, static_cast<uint8_t>(byte), &message,
                                                      &receivedStatus) == MAVLINK_FRAMING_OK &&
                            message.msgid == MAVLINK_MSG_ID_COMMAND_ACK &&
                            mavlink_msg_command_ack_get_command(&message) == MAV_CMD_PREFLIGHT_CALIBRATION) {
                            qInfo() << "MAV_ACK_TRACE wire" << QDateTime::currentMSecsSinceEpoch()
                                    << traceClock->elapsed() << message.sysid << message.compid;
                        }
                    }
                },
                Qt::DirectConnection);
            connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived, &traceContext,
                    [traceClock](LinkInterface*, const mavlink_message_t& message) {
                        if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK &&
                            mavlink_msg_command_ack_get_command(&message) == MAV_CMD_PREFLIGHT_CALIBRATION) {
                            qInfo() << "MAV_ACK_TRACE protocol" << QDateTime::currentMSecsSinceEpoch()
                                    << traceClock->elapsed() << message.sysid << message.compid;
                        }
                    });
            connect(vehicle, &Vehicle::mavCommandResult, &traceContext,
                    [traceClock](int, int, int command, int result, int failure) {
                        if (command == MAV_CMD_PREFLIGHT_CALIBRATION) {
                            qInfo() << "MAV_ACK_TRACE result" << QDateTime::currentMSecsSinceEpoch()
                                    << traceClock->elapsed() << result << failure;
                        }
                    });
            connect(&traceTimer, &QTimer::timeout, &traceContext,
                    [traceClock, lastTick = traceClock->elapsed()]() mutable {
                        const qint64 now = traceClock->elapsed();
                        if (now - lastTick > 200) {
                            qInfo() << "MAV_ACK_TRACE event-loop-gap" << QDateTime::currentMSecsSinceEpoch() << now
                                    << now - lastTick;
                        }
                        lastTick = now;
                    });
            traceTimer.start(25);
        }
        // -------------------------------------------------------------------------
        // Navigate to the Configure view
        // -------------------------------------------------------------------------
        navigateToConfigureView();
        if (QTest::currentTestFailed())
            return;

        // -------------------------------------------------------------------------
        // Click Summary
        // -------------------------------------------------------------------------
        QVERIFY2(clickButton(QStringLiteral("vehicleConfig_summary")),
                 qPrintable(QStringLiteral("%1: Failed to click Summary button").arg(vehicleName)));
        QTest::qWait(_viewDelay);

        // -------------------------------------------------------------------------
        // Click through each vehicle component, in English and Chinese
        // -------------------------------------------------------------------------
        clickThroughAllComponentsAllLocales(vehicle, vehicleName);
    });
}

// ---------------------------------------------------------------------------
// Per-vehicle-type test slots
// ---------------------------------------------------------------------------

void APMVehicleConfigUITest::_testArduCopter()
{
    _runNavigateVehicleConfig([] { return MockLink::startAPMArduCopterMockLink(); }, QStringLiteral("ArduCopter"));
}

void APMVehicleConfigUITest::_testArduPlane()
{
    _runNavigateVehicleConfig([] { return MockLink::startAPMArduPlaneMockLink(); }, QStringLiteral("ArduPlane"));
}

void APMVehicleConfigUITest::_testArduSub()
{
    ignoreLogMessage("qml", QtDebugMsg, QRegularExpression(QStringLiteral("^qrc:///qmlimages/Frames/Vectored\\.png$")));
    _runNavigateVehicleConfig([] { return MockLink::startAPMArduSubMockLink(); }, QStringLiteral("ArduSub"));
}

void APMVehicleConfigUITest::_testArduRover()
{
    _runNavigateVehicleConfig([] { return MockLink::startAPMArduRoverMockLink(); }, QStringLiteral("ArduRover"));
}

#ifdef Q_OS_HARMONY
void APMVehicleConfigUITest::_testHarmonyQueuedCalibrationDeadline_data()
{
    QTest::addColumn<int>("blockMs");
    QTest::addColumn<bool>("expectTimeout");
    QTest::newRow("within-production-deadline") << 750 << false;
    QTest::newRow("past-production-deadline") << 1350 << true;
}

void APMVehicleConfigUITest::_testHarmonyQueuedCalibrationDeadline()
{
    QFETCH(int, blockMs);
    QFETCH(bool, expectTimeout);

    runWithMockLink(
        [] { return MockLink::startAPMArduRoverMockLink(); },
        [&](QPointer<MockLink> mockLink, Vehicle* vehicle) {
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->isInitialConnectComplete(), TestTimeout::longMs());
            QTRY_VERIFY_WITH_TIMEOUT(vehicle->parameterManager()->parametersReady(), TestTimeout::longMs());
            const int vehicleId = vehicle->id();
            const int componentId = vehicle->defaultComponentId();
            QVERIFY(!vehicle->isMavCommandPending(componentId, MAV_CMD_PREFLIGHT_CALIBRATION));
            mockLink->clearReceivedMavCommandCounts();

            QObject controlledContext;
            int ackCount = 0;
            int ackResult = -1;
            int terminalCount = 0;
            int terminalResult = -1;
            int terminalFailure = -1;
            connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived, &controlledContext,
                    [&](LinkInterface* link, const mavlink_message_t& message) {
                        if (link != mockLink.data() || message.sysid != vehicleId || message.compid != componentId ||
                            message.msgid != MAVLINK_MSG_ID_COMMAND_ACK ||
                            mavlink_msg_command_ack_get_command(&message) != MAV_CMD_PREFLIGHT_CALIBRATION) {
                            return;
                        }
                        ++ackCount;
                        ackResult = mavlink_msg_command_ack_get_result(&message);
                        if (qEnvironmentVariableIsSet("QGC_TEST_MAV_ACK_TRACE")) {
                            qInfo() << "MAV_ACK_TRACE controlled-ack" << QDateTime::currentMSecsSinceEpoch() << ackCount
                                    << ackResult;
                        }
                    });
            connect(vehicle, &Vehicle::mavCommandResult, &controlledContext,
                    [&](int resultVehicleId, int targetComponent, int command, int result, int failure) {
                        if (resultVehicleId == vehicleId && targetComponent == componentId &&
                            command == MAV_CMD_PREFLIGHT_CALIBRATION) {
                            ++terminalCount;
                            terminalResult = result;
                            terminalFailure = failure;
                        }
                    });

            if (expectTimeout) {
                expectLogMessage(
                    "Vehicle.MavCommandQueue", QtWarningMsg,
                    QRegularExpression(QStringLiteral(
                        "^\"?Giving up sending command after max retries: MAV_CMD_PREFLIGHT_CALIBRATION\"?$")));
            }

            qint64 blockedMSecs = -1;
            QVERIFY(QMetaObject::invokeMethod(
                &controlledContext,
                [&]() {
                    QElapsedTimer blockTimer;
                    blockTimer.start();
                    vehicle->stopCalibration(false);
                    // Keep the newly posted MockLink write pending while the real response timer becomes overdue.
                    QThread::msleep(static_cast<unsigned long>(blockMs));
                    blockedMSecs = blockTimer.elapsed();
                },
                Qt::QueuedConnection));
            // A single dispatcher pass defers writes posted by the callback until the next pass.
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            const int terminalCountBeforeAck = terminalCount;
            if (qEnvironmentVariableIsSet("QGC_TEST_MAV_ACK_TRACE")) {
                qInfo() << "MAV_ACK_TRACE controlled-after-turn" << QDateTime::currentMSecsSinceEpoch() << blockedMSecs
                        << mockLink->receivedMavCommandCount(MAV_CMD_PREFLIGHT_CALIBRATION) << ackCount << terminalCount
                        << terminalResult << terminalFailure;
            }
            if (expectTimeout) {
                QVERIFY(blockedMSecs > 1200);
            } else {
                QVERIFY(blockedMSecs > 500);
                QVERIFY(blockedMSecs < 1200);
            }
            QCOMPARE(mockLink->receivedMavCommandCount(MAV_CMD_PREFLIGHT_CALIBRATION), 0);
            QCOMPARE(ackCount, 0);

            QTRY_COMPARE_WITH_TIMEOUT(mockLink->receivedMavCommandCount(MAV_CMD_PREFLIGHT_CALIBRATION), 1,
                                      TestTimeout::mediumMs());
            QTRY_COMPARE_WITH_TIMEOUT(ackCount, 1, TestTimeout::mediumMs());
            QCOMPARE(ackResult, static_cast<int>(MAV_RESULT_ACCEPTED));
            QTRY_COMPARE_WITH_TIMEOUT(terminalCount, 1, TestTimeout::mediumMs());
            if (expectTimeout) {
                verifyExpectedLogMessage();
                QCOMPARE(terminalCountBeforeAck, 1);
                QCOMPARE(terminalResult, static_cast<int>(MAV_RESULT_FAILED));
                QCOMPARE(terminalFailure, static_cast<int>(Vehicle::MavCmdResultFailureNoResponseToCommand));
            } else {
                QCOMPARE(terminalResult, static_cast<int>(MAV_RESULT_ACCEPTED));
                QCOMPARE(terminalFailure, static_cast<int>(Vehicle::MavCmdResultCommandResultOnly));
            }
            QVERIFY(!vehicle->isMavCommandPending(componentId, MAV_CMD_PREFLIGHT_CALIBRATION));
            QCOMPARE(terminalCount, 1);
        });
}
#endif
