"""Check a HarmonyOS HAP and record artifact plus supplied host-report evidence."""

import argparse
import hashlib
import json
import re
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ELF_MACHINES = {"arm64-v8a": (183, "AArch64"), "x86_64": (62, "x86_64")}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def build_id(stream, architecture="arm64-v8a"):
    stream.seek(0)
    header = stream.read(64)
    assert header[:6] == b"\x7fELF\x02\x01", "Expected little-endian ELF64"
    machine, label = ELF_MACHINES[architecture]
    assert struct.unpack_from("<H", header, 18)[0] == machine, f"Expected {label} ELF"
    offset = struct.unpack_from("<Q", header, 32)[0]
    entry_size, entries = struct.unpack_from("<HH", header, 54)
    for index in range(entries):
        stream.seek(offset + index * entry_size)
        entry = stream.read(entry_size)
        if struct.unpack_from("<I", entry)[0] != 4:
            continue
        note_offset = struct.unpack_from("<Q", entry, 8)[0]
        note_size = struct.unpack_from("<Q", entry, 32)[0]
        stream.seek(note_offset)
        notes = stream.read(note_size)
        cursor = 0
        while cursor + 12 <= len(notes):
            name_size, value_size, note_type = struct.unpack_from("<III", notes, cursor)
            cursor += 12
            name = notes[cursor : cursor + name_size]
            cursor += (name_size + 3) & ~3
            value = notes[cursor : cursor + value_size]
            cursor += (value_size + 3) & ~3
            if name == b"GNU\x00" and note_type == 3:
                return value.hex()
    raise AssertionError("ELF GNU build ID is missing")


def harmony_extras_evidence(archive, architecture, cmake_path):
    assert cmake_path.is_file(), "HarmonyOS platform source is missing"
    cmake = cmake_path.read_text(encoding="utf-8-sig")
    dependency = re.search(
        r"target_link_libraries\s*\([^)]*\bQt6::HarmonyExtrasPrivate\b[^)]*\)",
        re.sub(r"#[^\n]*", "", cmake),
        re.DOTALL,
    )
    library = f"libs/{architecture}/libQt6HarmonyExtras.so"
    evidence = {
        "required": dependency is not None,
        "dependency": " ".join(dependency.group().split()) if dependency else None,
        "dependencySource": str(cmake_path),
        "dependencySourceSha256": digest(cmake_path) if cmake_path.is_file() else None,
        "library": library,
        "present": library in archive.namelist(),
    }
    assert not evidence["required"] or evidence["present"], (
        f"Required Qt dependency missing: {library}"
    )
    if evidence["present"]:
        with archive.open(library) as stream:
            evidence["buildId"] = build_id(stream, architecture)
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--software-report", required=True, type=Path)
    parser.add_argument("--version-code", required=True, type=int)
    parser.add_argument("--architecture", choices=ELF_MACHINES, default="arm64-v8a")
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--build-log", type=Path)
    parser.add_argument("--hap", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    temporary_root = (root / "tmp").resolve()
    build_architecture = "arm64" if args.architecture == "arm64-v8a" else "x86_64"
    build = (
        args.build_root or root / f"tmp/build-v5.1.5-harmonyos-release-{build_architecture}"
    ).resolve()
    output = (args.output or root / "tmp/verification/qgc-communications-acceptance.json").resolve()
    assert output.is_relative_to(temporary_root), "Report must stay in project tmp"
    package_root = build / "harmonyos-build"
    hap = (
        args.hap or package_root / "entry/build/default/outputs/default/entry-default-signed.hap"
    ).resolve()
    assert hap.is_relative_to(temporary_root), "HAP must stay in project tmp"
    deployment_path = (build / "QGroundControl-harmony-deployment-settings.json").resolve()
    assert deployment_path.is_relative_to(build), (
        "Deployment settings must stay in the build directory"
    )
    deployment = json.loads(deployment_path.read_text(encoding="utf-8-sig"))
    binary = deployment.get("application-binary")
    assert isinstance(binary, str) and binary.strip(), (
        "Deployment settings have no application-binary"
    )
    native_binary = Path(binary)
    native_binary = (
        native_binary if native_binary.is_absolute() else build / native_binary
    ).resolve()
    assert native_binary.is_relative_to(build), (
        "application-binary must stay in the build directory"
    )
    assert native_binary.is_file(), "application-binary does not identify a built library"
    variant = (
        "signed"
        if hap.name.endswith("-signed.hap")
        else "unsigned"
        if hap.name.endswith("-unsigned.hap")
        else "unspecified"
    )
    build_log = (
        args.build_log
        or root / f"tmp/verification/qgc-v5.1.5-{build_architecture}-release-hap-build.log"
    ).resolve()
    build_output = build_log.read_text(encoding="utf-8-sig")
    assert "BUILD SUCCESSFUL" in build_output, "Build log has no BUILD SUCCESSFUL result"
    plain_build_output = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", build_output)
    sign_task_passed = bool(
        re.search(r"(?m)\b(?:Finished|UP-TO-DATE)\s+[^\r\n]*@SignHap\b", plain_build_output)
    )
    if variant == "signed":
        assert sign_task_passed, "Signed HAP needs a successful SignHap task in the build log"
    software = json.loads(args.software_report.read_text(encoding="utf-8-sig"))
    assert software["status"] == "passed", "Software suite failed"
    assert software["summary"]["failed"] == software["summary"]["skipped"] == 0
    assert software["summary"].get("componentsFailed", 0) == 0
    assert software["summary"].get("cancelled", 0) == software["summary"].get("todo", 0) == 0
    assert software["summary"]["componentsNotRun"] == software["summary"]["sdkErrors"] == 0

    hashes = {}
    for source in sorted((root / "deploy/harmonyos/bridge").glob("*.ets")):
        staged = package_root / "entry/src/main/ets/qability" / source.name
        assert digest(source) == digest(staged), f"Stale staged bridge: {source.name}"
        hashes[str(source.relative_to(root))] = digest(source)
    for name in (
        "src/Comms/UDPLink.cc",
        "src/Comms/TCPLink.cc",
        "src/Comms/Bluetooth/BluetoothLink.cc",
        "src/Comms/Bluetooth/BluetoothLink.h",
        "src/Comms/HarmonyBluetoothBridge.cc",
        "src/Comms/HarmonyBluetoothBridge.h",
        "src/AppSettings/BluetoothSettings.qml",
        "src/AppSettings/LinkConfigurationManager.qml",
    ):
        hashes[name] = digest(root / name)
    utils = (package_root / "entry/src/main/ets/qability/QtUtils.ets").read_text(
        encoding="utf-8-sig"
    )
    for name in ("QGCBluetoothBridge", "QGCBleBridge"):
        assert f"factories['{name}'] = get{name};" in utils, f"Factory not registered: {name}"

    with zipfile.ZipFile(hap) as archive:
        metadata = json.loads(archive.read("module.json"))
        app, module = metadata["app"], metadata["module"]
        assert app["versionName"] == "5.1.5" and app["versionCode"] == args.version_code
        assert app["bundleName"] == "org.mavlink.qgroundcontrol"
        assert app["minAPIVersion"] % 10000 == 23, "Expected minimum API23"
        permissions = {entry["name"] for entry in module["requestPermissions"]}
        assert {"ohos.permission.INTERNET", "ohos.permission.ACCESS_BLUETOOTH"} <= permissions
        ability = next(entry for entry in module["abilities"] if entry["name"] == "QAbility")
        assert ability["startWindowIconId"] == app["iconId"] == ability["iconId"]
        native = [entry for entry in archive.namelist() if entry.endswith(".so")]
        library_directory = f"libs/{args.architecture}/"
        machine, label = ELF_MACHINES[args.architecture]
        assert native and all(entry.startswith(library_directory) for entry in native), (
            f"Expected native libraries under {library_directory}"
        )
        for entry in native:
            with archive.open(entry) as stream:
                header = stream.read(64)
            assert header[:6] == b"\x7fELF\x02\x01", f"Not ELF64: {entry}"
            assert struct.unpack_from("<H", header, 18)[0] == machine, f"Not {label}: {entry}"
        with archive.open(f"{library_directory}{native_binary.name}") as stream:
            packaged_build_id = build_id(stream, args.architecture)
        with native_binary.open("rb") as stream:
            native_build_id = build_id(stream, args.architecture)
        assert packaged_build_id == native_build_id, (
            "HAP contains an older native application library"
        )
        harmony_extras = harmony_extras_evidence(
            archive, args.architecture, root / "cmake/platform/HarmonyOS.cmake"
        )
        bytecode = archive.read("ets/modules.abc")
        for name in (
            "QGCBluetoothBridge",
            "QGCBleBridge",
            "QGCSerialBridge",
            "QGCCdcDescriptors",
            "QGCCp210xDriver",
            "QGCCh34xDriver",
            "QGCFtdiDriver",
            "QGCWindowPolicy",
        ):
            assert name.encode() in bytecode, f"Module missing from Ark bytecode: {name}"
        license_path = "resources/rawfile/third_party/usb-serial-for-android/LICENSE.txt"
        assert (
            archive.read(license_path)
            == (root / "deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt").read_bytes()
        )

    report = {
        "verifiedUtc": datetime.now(timezone.utc).isoformat(),
        "qgcVersion": app["versionName"],
        "versionCode": app["versionCode"],
        "architecture": args.architecture,
        "elfMachine": machine,
        "minimumApi": 23,
        "nativeLibraries": len(native),
        "nativeBuildId": native_build_id,
        "qtHarmonyExtras": harmony_extras,
        "hap": str(hap),
        "bytes": hap.stat().st_size,
        "sha256": digest(hap),
        "nativeBinary": str(native_binary),
        "deploymentSettings": str(deployment_path),
        "arktsBuildPassed": True,
        "arktsSignedBuildPassed": variant == "signed" and sign_task_passed,
        "artifactVariant": variant,
        "signingEvidence": {
            "variantSource": "Selected HAP filename; no cryptographic signature inference",
            "signHapTaskSucceeded": sign_task_passed,
            "signatureVerification": "not_performed",
        },
        "buildLog": str(build_log),
        "buildLogSha256": digest(build_log),
        "buildWarningsPresent": "WARN:" in build_output,
        "softwareReport": str(args.software_report.resolve()),
        "softwareReportSha256": digest(args.software_report),
        "softwareSummary": software["summary"],
        "sourceSha256": hashes,
        "stagedBridgesMatch": True,
        "factoriesRegistered": True,
        "nativeBuildIdMatchesPackage": True,
        "launchIconPreserved": True,
        "usbSerialLicenseIncluded": True,
        "installedByAgent": False,
        "deviceActionsPerformed": False,
        "physicalAcceptancePerformed": False,
        "actionScope": "This checker invocation only",
        "testBoundaries": [
            "This report covers artifact checks and the supplied host software report only.",
            "Host components use loopback sockets, controlled Kit/provider APIs, and selected production QML as documented in that report.",
            "Independent target-runtime, full-application, and physical acceptance reports are not evaluated by this checker.",
            "Signing-task evidence is recorded separately from cryptographic HAP signature verification, which is not performed here.",
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "report": str(output),
                "sha256": report["sha256"],
                "versionCode": args.version_code,
                "nativeLibraries": len(native),
            },
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
