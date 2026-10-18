#include "HarmonyJoystickInputTest.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtTest/QTest>
#include <SDL3/SDL.h>
#include <limits>

#if defined(Q_OS_HARMONY) && !defined(QGC_HARMONY_JOYSTICK_NATIVE_TEST)
#include <syscap_ndk.h>
extern "C" const char* OH_GetProductModel(void);
#endif

#include "HarmonyJoystickInputTestHelper.h"

#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
#include <QtCore/QSettings>
#include <QtTest/QSignalSpy>

#include "Gimbal.h"
#include "GimbalController.h"
#include "Joystick.h"
#include "JoystickManager.h"
#include "LinkManager.h"
#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#endif

namespace {
int joystickCount()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    SDL_free(ids);
    return count;
}
}  // namespace

void HarmonyJoystickInputTest::initTestCase()
{
#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
    UnitTest::initTestCase();
#endif
    QVERIFY(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
}

void HarmonyJoystickInputTest::cleanupTestCase()
{
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
    UnitTest::cleanupTestCase();
#endif
}

void HarmonyJoystickInputTest::axisExtremes()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int instance = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(instance > 0);
    SDL_Joystick* joystick = SDL_GetJoystickFromID(instance);
    QVERIFY(joystick);
    HarmonyJoystickInputTestHelper::axis(input, 0, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 1, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 2, 2.0);
    HarmonyJoystickInputTestHelper::axis(input, 3, std::numeric_limits<double>::quiet_NaN());
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickAxis(joystick, 0), Sint16(-32768));
    QCOMPARE(SDL_GetJoystickAxis(joystick, 1), Sint16(32767));
    QCOMPARE(SDL_GetJoystickAxis(joystick, 2), Sint16(32767));
    QCOMPARE(SDL_GetJoystickAxis(joystick, 3), Sint16(0));
    HarmonyJoystickInputTestHelper::axis(input, 4, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 5, 1.0);
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickAxis(joystick, 4), Sint16(-32768));
    QCOMPARE(SDL_GetJoystickAxis(joystick, 5), Sint16(32767));
}

void HarmonyJoystickInputTest::buttonReleaseAndFocusLoss()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int instance = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(instance > 0);
    SDL_Joystick* joystick = SDL_GetJoystickFromID(instance);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH), true);
    HarmonyJoystickInputTestHelper::button(input, 2301, false);
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH), false);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    input.setFocused(false);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH), false);
    QCOMPARE(SDL_GetJoystickAxis(joystick, 0), Sint16(0));
    QCOMPARE(SDL_GetJoystickAxis(joystick, 4), Sint16(-32768));
    input.setFocused(true);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    SDL_UpdateJoysticks();
    QCOMPARE(SDL_GetJoystickButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH), true);
}

void HarmonyJoystickInputTest::detachReattachAndShiftedIndex()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int first = HarmonyJoystickInputTestHelper::add(input);
    const int second = HarmonyJoystickInputTestHelper::add(input, "second");
    QVERIFY(first > 0);
    QVERIFY(second > first);
    QCOMPARE(HarmonyJoystickInputTestHelper::add(input), first);
    HarmonyJoystickInputTestHelper::remove(input);
    QVERIFY(!SDL_GetJoystickFromID(first));
    QVERIFY(SDL_GetJoystickFromID(second));
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    const int third = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(third > second);
    HarmonyJoystickInputTestHelper::remove(input, "second");
    QVERIFY(!SDL_GetJoystickFromID(second));
    QVERIFY(SDL_GetJoystickFromID(third));
    input.stop();
    QVERIFY(!SDL_GetJoystickFromID(third));
}

void HarmonyJoystickInputTest::unsupportedDoesNotInventController()
{
#ifdef QGC_HARMONY_JOYSTICK_NATIVE_TEST
    HarmonyJoystickInput input;
    const int count = joystickCount();
    QCOMPARE(input.start(), HarmonyJoystickInput::Status::Unsupported);
    QCOMPARE(joystickCount(), count);
    QCOMPARE(input.start(), HarmonyJoystickInput::Status::Unsupported);
    QCOMPARE(joystickCount(), count);
#else
    // Real devices may monitor physical controllers; emulator behavior is verified by the runner.
    HarmonyJoystickInput input;
    const int count = joystickCount();
#ifdef Q_OS_HARMONY
    const QString model = QString::fromUtf8(OH_GetProductModel());
    const bool supported = canIUse("SystemCapability.Game.GameController");
    const bool expectedUnsupported = model == QStringLiteral("emulator") || !supported;
    if (expectedUnsupported) {
        expectLogMessage("default", QtInfoMsg,
                         QRegularExpression(QStringLiteral("^Harmony GameController Unsupported: model/SysCap ") +
                                            QRegularExpression::escape(
                                                QStringLiteral("\"%1\" %2").arg(model, supported ? "true" : "false")) +
                                            QStringLiteral("$")));
    }
#endif
    const auto status = input.start();
#ifdef Q_OS_HARMONY
    if (expectedUnsupported) {
        verifyExpectedLogMessage();
    }
#endif
    if (status != HarmonyJoystickInput::Status::Unsupported) {
        QSKIP("Unsupported-device test requires the emulator or a device without GameController support");
    }
    QCOMPARE(joystickCount(), count);
    input.stop();
    QCOMPARE(joystickCount(), count);
#endif
}

#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
void HarmonyJoystickInputTest::init()
{
    HarmonyJoystickTestBase::init();
    QSettings().remove(QStringLiteral("JoystickSettingsV2/QGC Test Gamepad Controller [first]"));
}

void HarmonyJoystickInputTest::cleanup()
{
    if (Joystick* joystick = JoystickManager::instance()->activeJoystick()) {
        joystick->stop();
    }
    (void) QMetaObject::invokeMethod(JoystickManager::instance(), "_checkForAddedOrRemovedJoysticks",
                                     Qt::DirectConnection);
    HarmonyJoystickTestBase::cleanup();
}

void HarmonyJoystickInputTest::_configureJoystick(Joystick* joystick)
{
    joystick->settings()->transmitterMode()->setRawValue(2);
    joystick->settings()->circleCorrection()->setRawValue(false);
    joystick->settings()->exponentialPct()->setRawValue(0);
    joystick->settings()->calibrated()->setRawValue(true);
    joystick->_setJoystickAxisForAxisFunction(Joystick::rollFunction, 2);
    joystick->_setJoystickAxisForAxisFunction(Joystick::pitchFunction, 3);
    joystick->_setJoystickAxisForAxisFunction(Joystick::yawFunction, 0);
    joystick->_setJoystickAxisForAxisFunction(Joystick::throttleFunction, 1);
    Joystick::AxisCalibration_t throttleCalibration;
    throttleCalibration.reversed = true;
    joystick->setAxisCalibration(1, throttleCalibration);
    Joystick::AxisCalibration_t pitchCalibration;
    pitchCalibration.reversed = true;
    joystick->setAxisCalibration(3, pitchCalibration);
}

void HarmonyJoystickInputTest::manualControlToMockLink()
{
    QCOMPARE(LinkManager::instance()->links().count(), 0);
    _connectMockLinkNoInitialConnectSequence();
    QVERIFY(_mockLink);
    QVERIFY(_vehicle);
    const auto primary = _vehicle->vehicleLinkManager()->primaryLink().lock();
    QVERIFY(primary);
    QCOMPARE(primary->linkConfiguration()->type(), LinkConfiguration::TypeMock);
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    QVERIFY(HarmonyJoystickInputTestHelper::add(input) > 0);
    auto* manager = JoystickManager::instance();
    (void) QMetaObject::invokeMethod(manager, "_checkForAddedOrRemovedJoysticks", Qt::DirectConnection);
    QVERIFY(QMetaObject::invokeMethod(manager, "_setActiveJoystickByName", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("QGC Test Gamepad Controller [first]"))));
    Joystick* joystick = manager->activeJoystick();
    QVERIFY(joystick);
    QVERIFY(!HarmonyJoystickInput::allowsVehicleControl(joystick->name(), nullptr));
    QVERIFY(HarmonyJoystickInput::allowsVehicleControl(joystick->name(), _vehicle));
    _configureJoystick(joystick);
    HarmonyJoystickInputTestHelper::axis(input, 0, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 1, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 2, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 3, -1.0);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    QSignalSpy bytes(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(bytes.isValid());
    QSignalSpy armActions(joystick, &Joystick::setArmed);
    QVERIFY(armActions.isValid());
    joystick->setButtonAction(SDL_GAMEPAD_BUTTON_SOUTH, QStringLiteral("Arm"));
    joystick->setButtonRepeat(SDL_GAMEPAD_BUTTON_SOUTH, false);
    joystick->settings()->buttonFrequencyHz()->setRawValue(50);
    joystick->settings()->calibrated()->setRawValue(true);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    const auto findControl = [&bytes](quint16 expectedButtons) {
        for (const QList<QVariant>& args : bytes) {
            mavlink_message_t receiveBuffer{}, message{};
            mavlink_status_t parseStatus{}, messageStatus{};
            const QByteArray packet = args.first().toByteArray();
            for (unsigned char byte : packet) {
                if (mavlink_frame_char_buffer(&receiveBuffer, &parseStatus, byte, &message, &messageStatus) ==
                        MAVLINK_FRAMING_OK &&
                    message.msgid == MAVLINK_MSG_ID_MANUAL_CONTROL) {
                    mavlink_manual_control_t control{};
                    mavlink_msg_manual_control_decode(&message, &control);
                    if (control.x == 1000 && control.y == 1000 && control.z == 0 && control.r == -1000 &&
                        control.buttons == expectedButtons)
                        return true;
                }
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findControl(1), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(armActions.count(), 1, 3000);
    bytes.clear();
    HarmonyJoystickInputTestHelper::button(input, 2301, false);
    QTRY_VERIFY_WITH_TIMEOUT(findControl(0), 3000);
    joystick->stop();
    const int armCount = armActions.count();
    QCoreApplication::sendPostedEvents(joystick);
    QCOMPARE(armActions.count(), armCount);
    input.stop();
    (void) QMetaObject::invokeMethod(manager, "_checkForAddedOrRemovedJoysticks", Qt::DirectConnection);
    QVERIFY(!manager->activeJoystick());
}

void HarmonyJoystickInputTest::continuousActionsStopOnDetach()
{
    QCOMPARE(LinkManager::instance()->links().count(), 0);
    _connectMockLink(MAV_AUTOPILOT_PX4, MockConfiguration::FailNone, MockConfiguration::OptionEnableGimbal);
    QVERIFY(_mockLink);
    QVERIFY(_vehicle);
    GimbalController* gimbalController = _vehicle->gimbalController();
    QVERIFY(gimbalController);
    QTRY_VERIFY_WITH_TIMEOUT(gimbalController->activeGimbal(), TestTimeout::longMs());
    Gimbal* gimbal = gimbalController->activeGimbal();
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    QVERIFY(HarmonyJoystickInputTestHelper::add(input) > 0);
    auto* manager = JoystickManager::instance();
    (void) QMetaObject::invokeMethod(manager, "_checkForAddedOrRemovedJoysticks", Qt::DirectConnection);
    QVERIFY(QMetaObject::invokeMethod(manager, "_setActiveJoystickByName", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("QGC Test Gamepad Controller [first]"))));
    Joystick* joystick = manager->activeJoystick();
    QVERIFY(joystick);
    _configureJoystick(joystick);
    joystick->setButtonAction(SDL_GAMEPAD_BUTTON_SOUTH, QStringLiteral("Gimbal Up"));
    joystick->setButtonRepeat(SDL_GAMEPAD_BUTTON_SOUTH, false);
    joystick->setButtonAction(SDL_GAMEPAD_BUTTON_EAST, QStringLiteral("Continuous Zoom In"));
    joystick->setButtonRepeat(SDL_GAMEPAD_BUTTON_EAST, false);
    QSignalSpy starts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy stops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy zoomStarts(joystick, &Joystick::startContinuousZoom);
    QSignalSpy zoomStops(joystick, &Joystick::stopContinuousZoom);
    QVERIFY(starts.isValid());
    QVERIFY(stops.isValid());
    QVERIFY(zoomStarts.isValid());
    QVERIFY(zoomStops.isValid());
    QStringList gimbalActions, zoomActions;
    QObject observer;
    (void) connect(joystick, &Joystick::gimbalPitchStart, &observer,
                   [&gimbalActions](int) { gimbalActions << "start"; });
    (void) connect(joystick, &Joystick::gimbalPitchStop, &observer, [&gimbalActions]() { gimbalActions << "stop"; });
    (void) connect(joystick, &Joystick::startContinuousZoom, &observer,
                   [&zoomActions](int) { zoomActions << "start"; });
    (void) connect(joystick, &Joystick::stopContinuousZoom, &observer, [&zoomActions]() { zoomActions << "stop"; });
    joystick->settings()->calibrated()->setRawValue(true);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::button(input, 2302, true);
    QTRY_COMPARE_WITH_TIMEOUT(starts.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(gimbal->pitchRate() > 0.0f, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(zoomStarts.count(), 1, 2000);
    QCOMPARE(stops.count(), 0);
    QCOMPARE(zoomStops.count(), 0);
    input.stop();
    (void) QMetaObject::invokeMethod(manager, "_checkForAddedOrRemovedJoysticks", Qt::DirectConnection);
    QTRY_COMPARE_WITH_TIMEOUT(stops.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(gimbal->pitchRate(), 0.0f, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(zoomStops.count(), 1, 3000);
    const QStringList expectedActions{"start", "stop"};
    QTRY_COMPARE_WITH_TIMEOUT(gimbalActions, expectedActions, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(zoomActions, expectedActions, 3000);
    QVERIFY(!manager->activeJoystick());
}
UT_REGISTER_TEST(HarmonyJoystickInputTest, TestLabel::Unit, TestLabel::Joystick)
#else
QTEST_GUILESS_MAIN(HarmonyJoystickInputTest)
#endif
