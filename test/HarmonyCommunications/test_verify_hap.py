"""Checker regression fixtures; synthetic artifacts are not application evidence."""

import contextlib
import importlib.util
import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch


SOURCE = Path(__file__).with_name("verify-hap.py")
SPEC = importlib.util.spec_from_file_location("verify_hap", SOURCE)
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
WORKSPACE = SOURCE.resolve().parents[2]
MODULES = ("QGCBluetoothBridge", "QGCBleBridge", "QGCSerialBridge",
           "QGCCdcDescriptors", "QGCCp210xDriver", "QGCCh34xDriver",
           "QGCFtdiDriver", "QGCWindowPolicy")
SOURCES = ("src/Comms/UDPLink.cc", "src/Comms/TCPLink.cc",
           "src/Comms/BluetoothLink.cc", "src/Comms/BluetoothLink.h",
           "src/Comms/HarmonyBluetoothBridge.cc", "src/Comms/HarmonyBluetoothBridge.h",
           "src/UI/AppSettings/BluetoothSettings.qml", "src/UI/AppSettings/LinkSettings.qml")


def elf(identity=b"\x11" * 20, machine=183):
    header = bytearray(64)
    header[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<H", header, 18, machine)
    struct.pack_into("<Q", header, 32, 64)
    struct.pack_into("<HH", header, 54, 56, 1)
    note = struct.pack("<III", 4, len(identity), 3) + b"GNU\x00" + identity
    program = struct.pack("<IIQQQQQQ", 4, 0, 120, 0, 0, len(note), len(note), 4)
    return bytes(header) + program + note


class VerifyHapTest(unittest.TestCase):
    def setUp(self):
        self.create_fixture()

    def create_fixture(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="verify-hap-", dir=WORKSPACE / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "tmp/build"
        self.package = self.build / "harmonyos-build"
        self.native = self.build / "Debug/libQGroundControl.so"
        self.settings = self.build / "QGroundControl-harmony-deployment-settings.json"
        self.hap = self.package / "entry/build/default/outputs/default/entry-default-signed.hap"
        self.log = self.root / "tmp/build.log"
        self.software = self.root / "tmp/host.json"
        self.output = self.root / "tmp/checked.json"
        for name in MODULES:
            contents = f"export class {name} {{}}\n".encode()
            self.write(self.root / f"deploy/harmonyos/bridge/{name}.ets", contents)
            self.write(self.package / f"entry/src/main/ets/qability/{name}.ets", contents)
        for name in SOURCES:
            self.write(self.root / name, b"controlled source\n")
        self.write(self.root / "deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt", b"license fixture\n")
        self.write(self.package / "entry/src/main/ets/qability/QtUtils.ets",
                   b"factories['QGCBluetoothBridge'] = getQGCBluetoothBridge;\n"
                   b"factories['QGCBleBridge'] = getQGCBleBridge;\n")
        self.write(self.native, elf())
        self.set_binary(str(self.native))
        self.write(self.log, b"> hvigor Finished :entry:default@SignHap... after 1 ms\nBUILD SUCCESSFUL\n")
        self.write(self.software, json.dumps({"status": "passed", "summary": {
            "failed": 0, "skipped": 0, "componentsNotRun": 0, "sdkErrors": 0,
            "tests": 1, "passed": 1}}).encode())
        self.metadata = {"app": {"versionName": "5.0.8", "versionCode": 5010520,
                                  "bundleName": "org.mavlink.qgroundcontrol",
                                  "minAPIVersion": 23, "iconId": 100},
                         "module": {"requestPermissions": [
                             {"name": "ohos.permission.INTERNET"},
                             {"name": "ohos.permission.ACCESS_BLUETOOTH"}],
                             "abilities": [{"name": "QAbility", "startWindowIconId": 100, "iconId": 100}]}}
        self.packaged_elf = elf()
        self.bytecode = " ".join(MODULES).encode()
        self.license = b"license fixture\n"
        self.write_hap()

    @staticmethod
    def write(path, contents):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)

    def set_binary(self, binary):
        self.write(self.settings, json.dumps({"application-binary": binary}).encode())

    def write_hap(self):
        self.hap.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(self.hap, "w") as archive:
            archive.writestr("module.json", json.dumps(self.metadata))
            archive.writestr("libs/arm64-v8a/libQGroundControl.so", self.packaged_elf)
            archive.writestr("ets/modules.abc", self.bytecode)
            archive.writestr("resources/rawfile/third_party/usb-serial-for-android/LICENSE.txt", self.license)

    def invoke(self, *extra):
        arguments = ["--software-report", str(self.software), "--version-code", "5010520",
                     "--build-root", str(self.build), "--build-log", str(self.log), "--output", str(self.output)]
        # Select a temporary project root; all checker algorithms remain real.
        with patch.object(CHECKER, "__file__", str(self.root / "test/HarmonyCommunications/verify-hap.py")), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with patch("sys.argv", [str(SOURCE), *arguments, *extra]):
                CHECKER.main()
        return json.loads(self.output.read_text(encoding="utf-8"))

    def checked(self, *extra):
        try:
            return self.invoke(*extra)
        except (AssertionError, FileNotFoundError, SystemExit) as error:
            self.fail(f"Valid checker input rejected: {type(error).__name__}: {error}")

    def test_default_signed_debug(self):
        report = self.checked()
        self.assertEqual(Path(report["hap"]), self.hap)
        self.assertTrue(report["arktsSignedBuildPassed"])
        self.assertEqual(report["signingEvidence"]["signatureVerification"], "not_performed")

    def test_absolute_release_binary(self):
        release = self.build / "Release/libQGroundControl.so"
        self.write(release, elf())
        self.native.unlink()
        self.set_binary(str(release))
        report = self.checked()
        self.assertEqual(Path(report["nativeBinary"]), release)
        self.assertTrue(report["nativeBuildIdMatchesPackage"])

    def test_relative_release_binary_and_unsigned_hap(self):
        self.write(self.build / "Release/libQGroundControl.so", elf())
        self.native.unlink()
        self.set_binary("Release/libQGroundControl.so")
        self.hap = self.hap.with_name("entry-default-unsigned.hap")
        self.write_hap()
        self.write(self.log, b"> hvigor Finished :entry:default@PackageHap...\nBUILD SUCCESSFUL\n")
        report = self.checked("--hap", str(self.hap))
        self.assertTrue(report["arktsBuildPassed"])
        self.assertFalse(report["arktsSignedBuildPassed"])
        self.assertEqual(report["artifactVariant"], "unsigned")
        self.assertEqual(report["signingEvidence"]["signatureVerification"], "not_performed")

    def test_renamed_hap_does_not_claim_signed_build(self):
        self.hap = self.root / "tmp/delivery/QGroundControl.hap"
        self.write_hap()
        report = self.checked("--hap", str(self.hap))
        self.assertFalse(report["arktsSignedBuildPassed"])
        self.assertEqual(report["artifactVariant"], "unspecified")
        self.assertEqual(report["signingEvidence"]["signatureVerification"], "not_performed")
        self.assertIn("checker", report["actionScope"].lower())

    def test_unsigned_does_not_inherit_sign_task_claim(self):
        self.hap = self.hap.with_name("entry-default-unsigned.hap")
        self.write_hap()
        report = self.checked("--hap", str(self.hap))
        self.assertTrue(report["signingEvidence"]["signHapTaskSucceeded"])
        self.assertFalse(report["arktsSignedBuildPassed"])

    def test_binary_escape_rejected(self):
        for binary in (str(self.root / "tmp/foreign/libQGroundControl.so"), "../foreign/libQGroundControl.so", ""):
            with self.subTest(binary=binary):
                self.write(self.root / "tmp/foreign/libQGroundControl.so", elf())
                self.set_binary(binary)
                with self.assertRaisesRegex(AssertionError, "application-binary|build directory"):
                    self.invoke()

    def test_explicit_hap_outside_project_tmp_rejected(self):
        self.hap = self.root / "foreign/entry-default-signed.hap"
        self.write_hap()
        with self.assertRaisesRegex(AssertionError, "HAP must stay in project tmp"):
            self.invoke("--hap", str(self.hap))

    def test_symlink_binary_escape_rejected(self):
        foreign = self.root / "tmp/foreign/libQGroundControl.so"
        self.write(foreign, elf())
        link = self.build / "linked.so"
        try:
            link.symlink_to(foreign)
        except OSError as error:
            self.skipTest(f"Host cannot create a symlink: {error.winerror}")
        self.set_binary(str(link))
        with self.assertRaisesRegex(AssertionError, "build directory"):
            self.invoke()

    def test_unsigned_still_requires_build_success(self):
        self.hap = self.hap.with_name("entry-default-unsigned.hap")
        self.write_hap()
        self.write(self.log, b"> hvigor Finished :entry:default@SignHap...\nBUILD FAILED\n")
        with self.assertRaises(AssertionError):
            self.invoke("--hap", str(self.hap))

    def test_signed_label_requires_completed_sign_task(self):
        self.write(self.log, b"> hvigor Started :entry:default@SignHap...\nBUILD SUCCESSFUL\n")
        with self.assertRaisesRegex(AssertionError, "SignHap"):
            self.invoke()

    def test_original_content_checks_reject_corruption(self):
        mutations = {
            "source": lambda: self.write(self.package / "entry/src/main/ets/qability/QGCBleBridge.ets", b"stale"),
            "factory": lambda: self.write(self.package / "entry/src/main/ets/qability/QtUtils.ets", b""),
            "version": lambda: self.metadata["app"].update(versionCode=1),
            "permission": lambda: self.metadata["module"].update(requestPermissions=[]),
            "icon": lambda: self.metadata["app"].update(iconId=999),
            "abi": lambda: setattr(self, "packaged_elf", elf(machine=62)),
            "build-id": lambda: setattr(self, "packaged_elf", elf(identity=b"\x22" * 20)),
            "bytecode": lambda: setattr(self, "bytecode", b""),
            "license": lambda: setattr(self, "license", b"wrong license"),
        }
        for name, mutate in mutations.items():
            with self.subTest(check=name):
                if name != "source":
                    self.create_fixture()
                mutate()
                self.write_hap()
                with self.assertRaises(AssertionError):
                    self.invoke()

    def test_failed_host_report_rejected(self):
        self.write(self.software, b'{"status":"failed"}')
        with self.assertRaisesRegex(AssertionError, "Software suite failed"):
            self.invoke()


if __name__ == "__main__":
    unittest.main()
