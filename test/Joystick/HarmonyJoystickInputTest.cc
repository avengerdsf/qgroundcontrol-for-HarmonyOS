#include "HarmonyJoystickInputTest.h"
#include "HarmonyJoystickInputTestHelper.h"

#include <SDL.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtTest/QTest>
#include <limits>

#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
#include "Joystick.h"
#include "JoystickManager.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include <QtCore/QSettings>
#include <QtTest/QSignalSpy>
#endif

void HarmonyJoystickInputTest::initTestCase()
{
    SDL_SetMainReady();
    QCOMPARE(SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER), 0);
}

void HarmonyJoystickInputTest::cleanupTestCase()
{
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
}

void HarmonyJoystickInputTest::axisExtremes()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int instance = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(instance >= 0);
    SDL_Joystick *joystick = SDL_JoystickFromInstanceID(instance);
    QVERIFY(joystick);
    HarmonyJoystickInputTestHelper::axis(input, 0, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 1, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 2, 2.0);
    HarmonyJoystickInputTestHelper::axis(input, 3, std::numeric_limits<double>::quiet_NaN());
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetAxis(joystick, 0), Sint16(-32768));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 1), Sint16(32767));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 2), Sint16(32767));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 3), Sint16(0));
    HarmonyJoystickInputTestHelper::axis(input, 4, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 5, 1.0);
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetAxis(joystick, 4), Sint16(-32768));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 5), Sint16(32767));
}

void HarmonyJoystickInputTest::buttonReleaseAndFocusLoss()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int instance = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(instance >= 0);
    SDL_Joystick *joystick = SDL_JoystickFromInstanceID(instance);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetButton(joystick, SDL_CONTROLLER_BUTTON_A), Uint8(1));
    HarmonyJoystickInputTestHelper::button(input, 2301, false);
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetButton(joystick, SDL_CONTROLLER_BUTTON_A), Uint8(0));
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    input.setFocused(false);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetButton(joystick, SDL_CONTROLLER_BUTTON_A), Uint8(0));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 0), Sint16(0));
    QCOMPARE(SDL_JoystickGetAxis(joystick, 4), Sint16(-32768));
    input.setFocused(true);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    SDL_JoystickUpdate();
    QCOMPARE(SDL_JoystickGetButton(joystick, SDL_CONTROLLER_BUTTON_A), Uint8(1));
}

void HarmonyJoystickInputTest::detachReattachAndShiftedIndex()
{
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    const int first = HarmonyJoystickInputTestHelper::add(input);
    const int second = HarmonyJoystickInputTestHelper::add(input, "second");
    QVERIFY(first >= 0);
    QVERIFY(second > first);
    QCOMPARE(HarmonyJoystickInputTestHelper::add(input), first);
    HarmonyJoystickInputTestHelper::remove(input);
    QVERIFY(!SDL_JoystickFromInstanceID(first));
    QVERIFY(SDL_JoystickFromInstanceID(second));
    HarmonyJoystickInputTestHelper::axis(input, 0, 1.0);
    const int third = HarmonyJoystickInputTestHelper::add(input);
    QVERIFY(third > second);
    HarmonyJoystickInputTestHelper::remove(input, "second");
    QVERIFY(!SDL_JoystickFromInstanceID(second));
    QVERIFY(SDL_JoystickFromInstanceID(third));
    input.stop();
    QVERIFY(!SDL_JoystickFromInstanceID(third));
}

void HarmonyJoystickInputTest::unsupportedDoesNotInventController()
{
#ifdef QGC_HARMONY_JOYSTICK_NATIVE_TEST
    HarmonyJoystickInput input;
    const int count = SDL_NumJoysticks();
    QCOMPARE(input.start(), HarmonyJoystickInput::Status::Unsupported);
    QCOMPARE(SDL_NumJoysticks(), count);
    QCOMPARE(input.start(), HarmonyJoystickInput::Status::Unsupported);
    QCOMPARE(SDL_NumJoysticks(), count);
#else
    // Real devices may monitor physical controllers; emulator behavior is verified by the runner.
    HarmonyJoystickInput input;
    const int count = SDL_NumJoysticks();
    const auto status = input.start();
    if (status != HarmonyJoystickInput::Status::Unsupported) {
        QSKIP("Unsupported-device test requires the emulator or a device without GameController support");
    }
    QCOMPARE(SDL_NumJoysticks(), count);
    input.stop();
    QCOMPARE(SDL_NumJoysticks(), count);
#endif
}

#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
void HarmonyJoystickInputTest::init()
{
    UnitTest::init();
    QSettings().remove(QStringLiteral("Joysticks/QGC Test Gamepad Controller [first]"));
}

void HarmonyJoystickInputTest::cleanup()
{
    if (Joystick *joystick = JoystickManager::instance()->activeJoystick()) {
        joystick->stop();
    }
    (void) QMetaObject::invokeMethod(JoystickManager::instance(), "_updateAvailableJoysticks", Qt::DirectConnection);
    _disconnectMockLink();
    UnitTest::cleanup();
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
    QVERIFY(HarmonyJoystickInputTestHelper::add(input) >= 0);
    auto *manager = JoystickManager::instance();
    (void) QMetaObject::invokeMethod(manager, "_updateAvailableJoysticks", Qt::DirectConnection);
    QVERIFY(manager->setActiveJoystickName(QStringLiteral("QGC Test Gamepad Controller [first]")));
    Joystick *joystick = manager->activeJoystick();
    QVERIFY(joystick);
    QVERIFY(!HarmonyJoystickInput::allowsVehicleControl(joystick->name(), nullptr));
    QVERIFY(HarmonyJoystickInput::allowsVehicleControl(joystick->name(), _vehicle));
    joystick->setTXMode(2);
    joystick->setCircleCorrection(false);
    joystick->setExponential(0);
    HarmonyJoystickInputTestHelper::axis(input, 0, -1.0);
    HarmonyJoystickInputTestHelper::axis(input, 1, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 2, 1.0);
    HarmonyJoystickInputTestHelper::axis(input, 3, -1.0);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    QSignalSpy bytes(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(bytes.isValid());
    QSignalSpy armActions(joystick, &Joystick::setArmed);
    QVERIFY(armActions.isValid());
    joystick->setButtonAction(SDL_CONTROLLER_BUTTON_A, QStringLiteral("Arm"));
    joystick->setButtonRepeat(SDL_CONTROLLER_BUTTON_A, true);
    joystick->setButtonFrequency(50);
    _vehicle->setJoystickEnabled(true);
    joystick->startPolling(_vehicle);
    const auto findControl = [&bytes](quint16 expectedButtons) {
        for (const QList<QVariant> &args : bytes) {
            mavlink_message_t receiveBuffer{}, message{};
            mavlink_status_t parseStatus{}, messageStatus{};
            const QByteArray packet = args.first().toByteArray();
            for (unsigned char byte : packet) {
                if (mavlink_frame_char_buffer(&receiveBuffer, &parseStatus, byte, &message, &messageStatus) == MAVLINK_FRAMING_OK &&
                    message.msgid == MAVLINK_MSG_ID_MANUAL_CONTROL) {
                    mavlink_manual_control_t control{};
                    mavlink_msg_manual_control_decode(&message, &control);
                    if (control.x == 1000 && control.y == 1000 && control.z == 0 && control.r == -1000 &&
                        control.buttons == expectedButtons) return true;
                }
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findControl(1), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(armActions.count() >= 2, 3000);
    bytes.clear();
    HarmonyJoystickInputTestHelper::button(input, 2301, false);
    QTRY_VERIFY_WITH_TIMEOUT(findControl(0), 3000);
    joystick->stop();
    const int armCount = armActions.count();
    QTest::qWait(60);
    QCOMPARE(armActions.count(), armCount);
    input.stop();
    (void) QMetaObject::invokeMethod(manager, "_updateAvailableJoysticks", Qt::DirectConnection);
    QVERIFY(!manager->activeJoystick());
}

void HarmonyJoystickInputTest::continuousActionsStopOnDetach()
{
    QCOMPARE(LinkManager::instance()->links().count(), 0);
    _connectMockLinkNoInitialConnectSequence();
    QVERIFY(_mockLink);
    QVERIFY(_vehicle);
    HarmonyJoystickInput input;
    HarmonyJoystickInputTestHelper::start(input);
    QVERIFY(HarmonyJoystickInputTestHelper::add(input) >= 0);
    auto *manager = JoystickManager::instance();
    (void) QMetaObject::invokeMethod(manager, "_updateAvailableJoysticks", Qt::DirectConnection);
    QVERIFY(manager->setActiveJoystickName(QStringLiteral("QGC Test Gamepad Controller [first]")));
    Joystick *joystick = manager->activeJoystick();
    QVERIFY(joystick);
    joystick->setButtonAction(SDL_CONTROLLER_BUTTON_A, QStringLiteral("Gimbal Up"));
    joystick->setButtonRepeat(SDL_CONTROLLER_BUTTON_A, false);
    joystick->setButtonAction(SDL_CONTROLLER_BUTTON_B, QStringLiteral("Continuous Zoom In"));
    joystick->setButtonRepeat(SDL_CONTROLLER_BUTTON_B, false);
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
    (void) connect(joystick, &Joystick::gimbalPitchStart, &observer, [&gimbalActions](int) { gimbalActions << "start"; });
    (void) connect(joystick, &Joystick::gimbalPitchStop, &observer, [&gimbalActions]() { gimbalActions << "stop"; });
    (void) connect(joystick, &Joystick::startContinuousZoom, &observer, [&zoomActions](int) { zoomActions << "start"; });
    (void) connect(joystick, &Joystick::stopContinuousZoom, &observer, [&zoomActions]() { zoomActions << "stop"; });
    _vehicle->setJoystickEnabled(true);
    joystick->startPolling(_vehicle);
    HarmonyJoystickInputTestHelper::button(input, 2301, true);
    HarmonyJoystickInputTestHelper::button(input, 2302, true);
    // Keep the start deliveries queued while stopping, to catch GUI-thread release reordering.
    QElapsedTimer deadline;
    deadline.start();
    while ((starts.count() == 0 || zoomStarts.count() == 0) && deadline.elapsed() < 2000) {
        QThread::msleep(1);
    }
    QCOMPARE(starts.count(), 1);
    QCOMPARE(zoomStarts.count(), 1);
    QCOMPARE(stops.count(), 0);
    QCOMPARE(zoomStops.count(), 0);
    input.stop();
    (void) QMetaObject::invokeMethod(manager, "_updateAvailableJoysticks", Qt::DirectConnection);
    QTRY_COMPARE_WITH_TIMEOUT(stops.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(zoomStops.count(), 1, 3000);
    const QStringList expectedActions{"start", "stop"};
    QTRY_COMPARE_WITH_TIMEOUT(gimbalActions, expectedActions, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(zoomActions, expectedActions, 3000);
    QVERIFY(!manager->activeJoystick());
}
#else
QTEST_GUILESS_MAIN(HarmonyJoystickInputTest)
#endif
