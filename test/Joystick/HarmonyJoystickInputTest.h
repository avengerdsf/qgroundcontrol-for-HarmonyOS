#pragma once

#ifdef QGC_HARMONY_JOYSTICK_NATIVE_TEST
#include <QtCore/QObject>
using HarmonyJoystickTestBase = QObject;
#else
#include "UnitTest.h"
using HarmonyJoystickTestBase = UnitTest;
#endif

class HarmonyJoystickInputTest : public HarmonyJoystickTestBase
{
    Q_OBJECT
#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
protected:
    void init() final;
    void cleanup() final;
#endif
private slots:
    void initTestCase();
    void cleanupTestCase();
    void axisExtremes();
    void buttonReleaseAndFocusLoss();
    void detachReattachAndShiftedIndex();
    void unsupportedDoesNotInventController();
#ifndef QGC_HARMONY_JOYSTICK_NATIVE_TEST
    void manualControlToMockLink();
    void continuousActionsStopOnDetach();
#endif
};
