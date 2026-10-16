#pragma once

#include "HarmonyJoystickInput.h"

class HarmonyJoystickInputTestHelper
{
public:
    static void start(HarmonyJoystickInput &input) {
        input._simulation = true;
        input._status = HarmonyJoystickInput::Status::Monitoring;
    }
    static int add(HarmonyJoystickInput &input, const QString &id = "first") {
        return input._deviceAdded(id, "Controller");
    }
    static void remove(HarmonyJoystickInput &input, const QString &id = "first") { input._deviceRemoved(id); }
    static void axis(HarmonyJoystickInput &input, int axis, double value, const QString &id = "first") {
        input._axis(id, axis, value);
    }
    static void button(HarmonyJoystickInput &input, int keyCode, bool down, const QString &id = "first") {
        input._button(id, keyCode, down);
    }
};
