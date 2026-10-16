"""Run the real device UI branches, CMake metadata, and QML expressions on a host."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TEST_ROOT = Path(__file__).resolve().parent


def function_source(text, signature):
    start = text.find(signature)
    if start < 0:
        return ""
    opening = text.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (text[position] == "{") - (text[position] == "}")
        position += 1
    return text[start:position]


def binding(path, anchor, property_name="visible"):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.rfind("\n", 0, text.index(anchor))
    match = re.search(r"\n +" + re.escape(property_name) + r":\s*([^\n]+)", text[start:])
    if not match:
        raise ValueError(f"Missing {property_name} binding at {anchor}")
    value = match[1]
    for line in text[start + match.end():].splitlines():
        if line.lstrip().startswith("&&"):
            value += " " + line.strip()
        elif line.strip():
            break
    return value


class DeviceUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work_root = ROOT / "tmp"
        work_root.mkdir(exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix="harmony-device-ui-", dir=work_root)
        cls.work_path = Path(cls.work.name)
        cls.env = os.environ.copy()
        cls.env["PYTHONDONTWRITEBYTECODE"] = "1"
        for name in ("TEMP", "TMP", "TMPDIR"):
            cls.env[name] = cls.work.name
        cls.env["PATH"] = str(Path(ARGS.cxx).parent) + os.pathsep + cls.env.get("PATH", "")
        for name, content in {
            "QtCore/QObject": '#pragma once\n#include <string>\n#define Q_OBJECT\n'
                '#define Q_PROPERTY(...)\n#define Q_INVOKABLE\nclass QObject {};\n'
                'class QString { std::string value; public: QString() = default; '
                'QString(const char *text): value(text) {} '
                'static QString fromStdString(const std::string &text) { return QString(text.c_str()); } '
                'bool operator==(const QString &other) const { return value == other.value; } };\n'
                '#define QStringLiteral(text) QString(text)\n',
            "QtCore/QLoggingCategory": "#pragma once\n#define Q_DECLARE_LOGGING_CATEGORY(...)\n",
            "QtQmlIntegration/QtQmlIntegration": "#pragma once\n",
        }.items():
            path = cls.work_path / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        source = (ROOT / "src/QmlControls/ScreenToolsController.cc").read_text(encoding="utf-8")
        methods = "#ifdef Q_OS_HARMONY\n"
        methods += function_source(source, "const QString &harmonyDeviceType()") + "\n#endif\n"
        methods += "\n" + function_source(source, "bool ScreenToolsController::isHarmonyPhone()")
        methods += "\n" + function_source(source, "bool ScreenToolsController::isHarmonyTablet()")
        methods += "\n#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)\n"
        methods += function_source(source, "bool ScreenToolsController::fakeMobile()") + "\n#endif\n"
        (cls.work_path / "ScreenToolsDeviceMethods.inc").write_text(methods, encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_command(self, command):
        result = subprocess.run(command, cwd=ROOT, env=self.env, capture_output=True,
                                text=True, errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def evaluate(self, expression, mobile=False, phone=False, serial=True, **state):
        context = {
            "ScreenTools": {"isMobile": mobile, "isHarmonyPhone": phone,
                            "isSerialAvailable": serial},
            "_corePlugin": {"options": {"showFirmwareUpgrade": True}},
            "_appSavePath": {"visible": True}, "_brandImageSettings": {"visible": True},
            "_locationType": 1, "RemoteIDIndicatorPage": {"LocationType": {"TAKEOFF": 0}},
            "QGroundControl": {"settingsManager": {"autoConnectSettings": {
                "autoConnectNmeaPort": {"visible": True}, "autoConnectNmeaBaud": {"visible": True}}}},
        }
        context.update(state)
        script = "const vm=require('node:vm');const p=JSON.parse(process.argv[1]);" \
                 "process.stdout.write(JSON.stringify(vm.runInNewContext(p.expression,p.context)));"
        return json.loads(self.run_command([ARGS.node, "-e", script, json.dumps({
            "expression": expression, "context": context})]))

    def test_real_controller_device_classification_and_single_cached_read(self):
        platforms = (("harmony", "Q_OS_HARMONY", 0, 0, 0),
                     ("desktop", "Q_OS_WIN", 0, 0, 0),
                     ("android", "Q_OS_ANDROID", 1, 1, 0), ("ios", "Q_OS_IOS", 1, 0, 1))
        for name, define, native, android, ios in platforms:
            with self.subTest(platform=name):
                executable = self.work_path / f"{name}.exe"
                self.run_command([ARGS.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(self.work_path), "-I" + str(ROOT / "src/QmlControls"),
                    "-D" + define, f"-DTEST_NATIVE_MOBILE={native}",
                    f"-DTEST_ANDROID={android}", f"-DTEST_IOS={ios}",
                    str(TEST_ROOT / "ScreenToolsDeviceTest.cc"), "-o", str(executable)])
                for device, failure in (("phone", 0), ("tablet", 0), ("2in1", 0),
                                        ("unknown", 0), ("", 0), ("phone", 1)):
                    with self.subTest(device=device, failure=failure):
                        harmony = name == "harmony" and not failure
                        phone = harmony and device == "phone"
                        tablet = harmony and device == "tablet"
                        self.run_command([str(executable), device, str(failure), str(int(phone)),
                                          str(int(tablet)), str(int(phone or native))])

    def test_unified_metadata_and_existing_platform_checks(self):
        self.run_command([ARGS.cmake, f"-DSOURCE_ROOT={ROOT.as_posix()}", "-P",
                          str(TEST_ROOT / "HarmonyDeviceMetadataTest.cmake")])
        self.run_command([ARGS.cmake, "-P", str(ROOT / "cmake/tests/HarmonyOSConfigTest.cmake")])

    def test_phone_feature_visibility_preserves_other_mobile_platforms(self):
        features = (
            ("src/Vehicle/VehicleSetup/SetupView.qml", "id:                 firmwareButton"),
            ("src/UI/AppSettings/GeneralSettings.qml", "visible:            _appSavePath.visible"),
            ("src/UI/AppSettings/GeneralSettings.qml", 'heading:            qsTr("Brand Image")'),
            ("src/UI/AppSettings/RemoteIDSettings.qml", "id:                         gpsGrid"),
        )
        for path, anchor in features:
            with self.subTest(feature=anchor):
                expression = binding(path, anchor)
                self.assertTrue(self.evaluate(expression, mobile=True, phone=True))
                self.assertTrue(self.evaluate(expression))
                self.assertFalse(self.evaluate(expression, mobile=True))
        firmware = binding(*features[0])
        self.assertFalse(self.evaluate(firmware, mobile=True, phone=True, serial=False))
        self.assertFalse(self.evaluate(firmware, _corePlugin={"options": {"showFirmwareUpgrade": False}}))
        setup = (ROOT / features[0][0]).read_text(encoding="utf-8")
        hint = next(line.strip() for line in setup.splitlines() if " Click Firmware on the left" in line)
        for state in ({}, {"mobile": True}, {"mobile": True, "phone": True},
                      {"mobile": True, "phone": True, "serial": False},
                      {"_corePlugin": {"options": {"showFirmwareUpgrade": False}}}):
            self.assertEqual(bool(self.evaluate(hint, **state)), self.evaluate(firmware, **state))
        self.assertFalse(self.evaluate(binding(*features[1]), _appSavePath={"visible": False}))
        self.assertFalse(self.evaluate(binding(*features[2]), _brandImageSettings={"visible": False}))
        nmea = binding(*features[3])
        self.assertFalse(self.evaluate(nmea, mobile=True, phone=True, serial=False))
        self.assertFalse(self.evaluate(nmea, _locationType=0))
        for port, baud in ((False, True), (True, False)):
            self.assertFalse(self.evaluate(nmea, QGroundControl={"settingsManager": {
                "autoConnectSettings": {"autoConnectNmeaPort": {"visible": port},
                                        "autoConnectNmeaBaud": {"visible": baud}}}}))

    def test_cancelled_serial_link_bindings_and_flow_handler(self):
        serial = "src/UI/AppSettings/SerialSettings.qml"
        cases = (
            ("src/UI/AppSettings/LinkSettings.qml", "id:     linkSettingsLoader", "source", "", "serial.qml"),
            (serial, 'qsTr("Enable Flow Control")', "checked", False, True),
            (serial, 'qsTr("Data Bits")', "currentIndex", -1, 3),
            (serial, 'qsTr("Stop Bits")', "currentIndex", -1, 1),
        )
        config = {"settingsURL": "serial.qml", "flowControl": 1, "dataBits": 8, "stopBits": 2}
        for path, anchor, prop, empty, configured in cases:
            with self.subTest(binding=anchor):
                expression = binding(path, anchor, prop)
                self.assertEqual(self.evaluate(expression, subEditConfig=None), empty)
                self.assertEqual(self.evaluate(expression, subEditConfig=config), configured)
        handler = binding(serial, 'qsTr("Enable Flow Control")', "onCheckedChanged")
        callback = "(function(){" + handler + ";return subEditConfig;})()"
        self.assertIsNone(self.evaluate(callback, subEditConfig=None, checked=False))
        for checked, flow in ((False, 0), (True, 1)):
            self.assertEqual(self.evaluate(callback, subEditConfig=config, checked=checked)["flowControl"], flow)

    def test_apm_charts_only_add_harmony_phone_to_existing_desktop_support(self):
        text = (ROOT / "src/AutoPilotPlugins/APM/APMTuningComponentCopter.qml").read_text(encoding="utf-8")
        expression = re.search(r"^\s+showAdvanced = ([^\n]+)", text, re.M)[1]
        self.assertTrue(self.evaluate(expression, mobile=True, phone=True))
        self.assertTrue(self.evaluate(expression))
        self.assertFalse(self.evaluate(expression, mobile=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--node", required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
