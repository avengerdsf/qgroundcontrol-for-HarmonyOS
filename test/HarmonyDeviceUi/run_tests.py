"""Exercise production device classification, metadata and current v5.1 UI bindings."""

import argparse
import ctypes
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

if os.name == "nt":
    ctypes.windll.kernel32.SetErrorMode(0x8003)

ROOT = Path(__file__).resolve().parents[2]
TEST_ROOT = Path(__file__).resolve().parent


def function_source(text, signature):
    start = text.find(signature)
    if start < 0:
        return ""
    opening = text.index("{", start)
    depth, position = 1, opening + 1
    while depth:
        depth += (text[position] == "{") - (text[position] == "}")
        position += 1
    return text[start:position]


def binding(path, anchor, property_name="visible"):
    source = (ROOT / path).read_text(encoding="utf-8")
    start = source.rfind("\n", 0, source.index(anchor))
    match = re.search(r"\n +" + re.escape(property_name) + r":\s*([^\n]+)", source[start:])
    if not match:
        raise ValueError(f"Missing {property_name} binding at {anchor}")
    value = match[1]
    for line in source[start + match.end() :].splitlines():
        if not line.strip():
            continue
        if line.lstrip().startswith(("&&", "||")) or value.rstrip().endswith(("&&", "||")):
            value += " " + line.strip()
        else:
            break
    return value


class DeviceUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work_root = ROOT / "tmp/cache/host-tests"
        work_root.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix="harmony-device-ui-", dir=work_root)
        cls.work_path = Path(cls.work.name)
        cls.env = os.environ.copy()
        cls.env["PYTHONDONTWRITEBYTECODE"] = "1"
        for name in ("TEMP", "TMP", "TMPDIR"):
            cls.env[name] = cls.work.name
        compiler = Path(ARGS.cxx).resolve()
        cls.env["PATH"] = str(compiler.parent) + os.pathsep + cls.env.get("PATH", "")
        if os.name == "nt":
            for name in ("libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll"):
                runtime = compiler.parent / name
                if not runtime.is_file():
                    raise RuntimeError(f"Missing compiler runtime: {runtime}")
            frontend = subprocess.run(
                [str(compiler), "-print-prog-name=cc1plus"],
                cwd=ROOT,
                env=cls.env,
                capture_output=True,
                text=True,
                check=True,
                timeout=10,
            ).stdout.strip()
            subprocess.run(
                [frontend, "--version"],
                cwd=ROOT,
                env=cls.env,
                check=True,
                stdout=subprocess.DEVNULL,
                timeout=10,
            )
        for name, content in {
            "QtCore/QObject": "#pragma once\n#include <string>\n#define Q_OBJECT\n"
            "#define Q_PROPERTY(...)\n#define Q_INVOKABLE\nclass QObject {};\n"
            "class QString { std::string value; public: QString() = default; "
            "QString(const char *text): value(text) {} "
            "static QString fromStdString(const std::string &text) { return QString(text.c_str()); } "
            "bool operator==(const QString &other) const { return value == other.value; } };\n"
            "#define QStringLiteral(text) QString(text)\n",
            "QtCore/QLoggingCategory": "#pragma once\n#define Q_DECLARE_LOGGING_CATEGORY(...)\n",
            "QtQmlIntegration/QtQmlIntegration": "#pragma once\n#define QML_ELEMENT\n#define QML_SINGLETON\n",
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
        methods += (
            function_source(source, "bool ScreenToolsController::fakeMobile()") + "\n#endif\n"
        )
        (cls.work_path / "ScreenToolsDeviceMethods.inc").write_text(methods, encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_command(self, command):
        result = subprocess.run(
            command,
            cwd=ROOT,
            env=self.env,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def evaluate(self, expression, mobile=False, phone=False, serial=True, **state):
        context = {
            "ScreenTools": {
                "isMobile": mobile,
                "isHarmonyPhone": phone,
                "isSerialAvailable": serial,
            },
            "_corePlugin": {"options": {"showFirmwareUpgrade": True}},
            "vehicleConfigView": {"_searchQuery": ""},
            "_searchQuery": "",
            "_locationType": 1,
            "RemoteIDSettings": {"LocationType": {"TAKEOFF": 0}},
            "_autoConnectSettings": {"nmeaSource": {"rawValue": 2}},
            "AutoConnectSettings": {"NmeaSourceSerial": 2, "NmeaSourceUdp": 1},
            "QGroundControl": {
                "settingsManager": {
                    "autoConnectSettings": {
                        "nmeaSource": {"userVisible": True},
                        "autoConnectNmeaBaud": {"userVisible": True},
                    }
                }
            },
        }
        context.update(state)
        script = (
            "const vm=require('node:vm');const p=JSON.parse(process.argv[1]);"
            "process.stdout.write(JSON.stringify(vm.runInNewContext(p.expression,p.context)));"
        )
        return json.loads(
            self.run_command(
                [
                    ARGS.node,
                    "-e",
                    script,
                    json.dumps({"expression": expression, "context": context}),
                ]
            )
        )

    def test_real_controller_device_classification_and_single_cached_read(self):
        platforms = (
            ("harmony", "Q_OS_HARMONY", 0, 0, 0),
            ("desktop", "Q_OS_WIN", 0, 0, 0),
            ("android", "Q_OS_ANDROID", 1, 1, 0),
            ("ios", "Q_OS_IOS", 1, 0, 1),
        )
        for name, define, native, android, ios in platforms:
            with self.subTest(platform=name):
                executable = self.work_path / f"{name}.exe"
                self.run_command(
                    [
                        ARGS.cxx,
                        "-std=c++17",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I" + str(self.work_path),
                        "-I" + str(ROOT / "src/QmlControls"),
                        "-D" + define,
                        f"-DTEST_NATIVE_MOBILE={native}",
                        f"-DTEST_ANDROID={android}",
                        f"-DTEST_IOS={ios}",
                        str(TEST_ROOT / "ScreenToolsDeviceTest.cc"),
                        "-o",
                        str(executable),
                    ]
                )
                for device, failure in (
                    ("phone", 0),
                    ("tablet", 0),
                    ("2in1", 0),
                    ("unknown", 0),
                    ("", 0),
                    ("phone", 1),
                ):
                    with self.subTest(device=device, failure=failure):
                        harmony = name == "harmony" and not failure
                        phone, tablet = (
                            harmony and device == "phone",
                            harmony and device == "tablet",
                        )
                        self.run_command(
                            [
                                str(executable),
                                device,
                                str(failure),
                                str(int(phone)),
                                str(int(tablet)),
                                str(int(phone or native)),
                            ]
                        )

    def test_unified_metadata_and_existing_platform_checks(self):
        self.run_command(
            [
                ARGS.cmake,
                f"-DSOURCE_ROOT={ROOT.as_posix()}",
                "-P",
                str(TEST_ROOT / "HarmonyDeviceMetadataTest.cmake"),
            ]
        )
        self.run_command([ARGS.cmake, "-P", str(ROOT / "cmake/tests/HarmonyOSConfigTest.cmake")])

    def test_phone_feature_visibility_preserves_other_mobile_platforms(self):
        setup = "src/Vehicle/VehicleSetup/VehicleConfigView.qml"
        remote = "src/AppSettings/RemoteIDGpsLocation.qml"
        firmware = binding(setup, "id:                 firmwareButton")
        nmea = binding(remote, 'heading:            qsTr("NMEA External GPS")')
        general = json.loads(
            (ROOT / "src/AppSettings/pages/General.SettingsUI.json").read_text(encoding="utf-8")
        )
        save_path = next(
            control
            for group in general["groups"]
            for control in group["controls"]
            if control.get("setting") == "appSettings.savePath"
        )
        self.assertEqual(save_path["control"], "browse")
        for name, expression in (
            ("firmware", firmware),
            ("NMEA", nmea),
            ("savePath", save_path["showWhen"]),
        ):
            for mobile, phone, want in (
                (True, True, True),
                (False, False, True),
                (True, False, False),
            ):
                with self.subTest(feature=name, mobile=mobile, harmonyPhone=phone):
                    self.assertEqual(self.evaluate(expression, mobile=mobile, phone=phone), want)
        self.assertFalse(self.evaluate(firmware, mobile=True, phone=True, serial=False))
        self.assertFalse(self.evaluate(nmea, mobile=True, phone=True, serial=False))
        # v5.1 desktop supports non-serial firmware paths and NMEA UDP independently of USB serial.
        self.assertTrue(self.evaluate(firmware, serial=False))
        self.assertTrue(self.evaluate(nmea, serial=False))
        self.assertFalse(
            self.evaluate(firmware, _corePlugin={"options": {"showFirmwareUpgrade": False}})
        )
        self.assertFalse(self.evaluate(firmware, vehicleConfigView={"_searchQuery": "flight"}))
        self.assertFalse(self.evaluate(nmea, _locationType=0))
        for source, baud in ((False, True), (True, False)):
            self.assertFalse(
                self.evaluate(
                    nmea,
                    QGroundControl={
                        "settingsManager": {
                            "autoConnectSettings": {
                                "nmeaSource": {"userVisible": source},
                                "autoConnectNmeaBaud": {"userVisible": baud},
                            }
                        }
                    },
                )
            )
        route = binding(setup, "id:                 firmwareButton", "onClicked")
        routed = self.evaluate(
            "(function(){function showPanel(kind,url){return [kind,url]} return " + route + ";})()"
        )
        self.assertEqual(
            routed, ["firmware", "qrc:/qml/QGroundControl/VehicleSetup/FirmwareUpgrade.qml"]
        )
        self.assertTrue((ROOT / "src/Vehicle/VehicleSetup/FirmwareUpgrade.qml").is_file())

    def test_new_nmea_source_controls_preserve_serial_and_udp(self):
        path = "src/AppSettings/RemoteIDGpsLocation.qml"
        source = (ROOT / path).read_text(encoding="utf-8")
        serial_source = re.search(r"_serialSource:\s*([^\n]+)", source)[1]
        udp_visible = binding(path, 'label:              qsTr("UDP Port")')
        for value, want in ((2, True), (1, False), (0, False)):
            self.assertEqual(
                self.evaluate(
                    serial_source, _autoConnectSettings={"nmeaSource": {"rawValue": value}}
                ),
                want,
            )
            self.assertEqual(
                self.evaluate(
                    udp_visible, _autoConnectSettings={"nmeaSource": {"rawValue": value}}
                ),
                value == 1,
            )

    def test_cancelled_serial_link_bindings_and_flow_handler(self):
        serial = "src/AppSettings/SerialSettings.qml"
        cases = (
            (
                "src/AppSettings/LinkConfigurationManager.qml",
                "id:     linkSettingsLoader",
                "source",
                "",
                "serial.qml",
            ),
            (serial, 'qsTr("Enable Flow Control")', "checked", False, True),
            (serial, 'qsTr("Data Bits")', "currentIndex", -1, 3),
            (serial, 'qsTr("Stop Bits")', "currentIndex", -1, 1),
        )
        config = {"settingsURL": "serial.qml", "flowControl": 1, "dataBits": 8, "stopBits": 2}
        for path, anchor, prop, empty, configured in cases:
            key = (
                "editingConfig"
                if path.endswith("LinkConfigurationManager.qml")
                else "subEditConfig"
            )
            with self.subTest(binding=anchor):
                expression = binding(path, anchor, prop)
                self.assertEqual(self.evaluate(expression, **{key: None}), empty)
                self.assertEqual(self.evaluate(expression, **{key: config}), configured)
        handler = binding(serial, 'qsTr("Enable Flow Control")', "onCheckedChanged")
        callback = "(function(){" + handler + ";return subEditConfig;})()"
        self.assertIsNone(self.evaluate(callback, subEditConfig=None, checked=False))
        for checked, flow in ((False, 0), (True, 1)):
            self.assertEqual(
                self.evaluate(callback, subEditConfig=config, checked=checked)["flowControl"], flow
            )

    def test_advanced_tuning_uses_new_unrestricted_component(self):
        registration = (ROOT / "src/AutoPilotPlugins/APM/APMAutoPilotPlugin.cc").read_text(
            encoding="utf-8"
        )
        branch = function_source(registration, "if (_vehicle->multiRotor())")
        self.assertIn("new APMAdvancedTuningCopterComponent", branch)
        self.assertIn("_components.append", branch)
        component = (
            ROOT / "src/AutoPilotPlugins/APM/APMAdvancedTuningCopterComponent.cc"
        ).read_text(encoding="utf-8")
        self.assertIn("APMAdvancedTuningCopterComponent.qml", component)
        self.assertNotRegex(
            branch + component, r"isMobile|isHarmonyPhone|Q_OS_(ANDROID|IOS|HARMONY)"
        )
        view = (ROOT / "src/Vehicle/VehicleSetup/VehicleConfigView.qml").read_text(encoding="utf-8")
        visible = function_source(view, "function _componentVisible(component)")
        expression = "(" + visible + ")({setupSource:'APMAdvancedTuningCopterComponent.qml'})"
        for mobile, phone in ((False, False), (True, False), (True, True)):
            self.assertTrue(self.evaluate(expression, mobile=mobile, phone=phone))
        self.assertIn(
            "PIDTuning {",
            (ROOT / "src/AutoPilotPlugins/APM/APMAdvancedTuningCopterComponent.qml").read_text(
                encoding="utf-8"
            ),
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--node", required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
