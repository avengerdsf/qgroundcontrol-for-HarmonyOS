"""Summarize native Qt results and verify the files compiled for this run."""

import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--test-exit", type=int, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    build = args.build_directory.resolve()
    if not build.is_relative_to(repository / "tmp"):
        raise ValueError("Reports must stay inside the project tmp directory")
    manifest_path = build / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    changed = [path for path, digest in manifest.items() if sha256(repository / path) != digest]
    result_path = build / "result.xml"
    native = ET.parse(result_path).getroot()
    cases = native.findall("testcase")
    lifecycle = [case for case in cases if case.get("name") in ("initTestCase", "cleanupTestCase")]
    failed = sum(
        case.find("failure") is not None or case.find("error") is not None for case in cases
    )
    skipped = sum(case.find("skipped") is not None for case in cases)
    fixture = json.loads((Path(__file__).parent / "accel-replay.json").read_text(encoding="utf-8"))
    snapshot = sha256(manifest_path)
    snapshot_logged = f"sourceSnapshot={snapshot}" in (build / "result.txt").read_text(
        encoding="utf-8"
    )
    passed = (
        args.test_exit == 0 and failed == 0 and skipped == 0 and not changed and snapshot_logged
    )
    report = {
        "result": "passed" if passed else "failed",
        "runtime": "Windows host Qt 6.12.0, QGuiApplication offscreen/software",
        "testExitCode": args.test_exit,
        "nativeCases": len(cases),
        "functionalCases": len(cases) - len(lifecycle),
        "lifecycleCases": len(lifecycle),
        "failed": failed,
        "skipped": skipped,
        "nativeXml": str(result_path),
        "nativeXmlSha256": sha256(result_path),
        "executableSha256": sha256(build / "SensorCalibrationTest.exe"),
        "sourceSnapshot": snapshot,
        "compiledSnapshotMatches": snapshot_logged,
        "filesChangedAfterConfigure": changed,
        "sourceManifest": manifest,
        "replayedMessages": {name: len(rows) for name, rows in fixture["sessions"].items()},
        "sourceTelemetrySha256": fixture["sourceSha256"],
        "productionCompiledWhole": [
            "src/AutoPilotPlugins/PX4/SensorsComponentController.cc",
            "src/MAVLink/StatusTextHandler.cc",
        ],
        "fixtureBoundary": [
            "Vehicle/FactPanelController/ParameterManager/application/logging services are small test shells",
            "QQuickItem properties/signals, MAVLink pack/CRC/wire decode and StatusTextHandler are real",
            "Vehicle hardware I/O, full Vehicle::_textMessageReceived and Pad GUI rendering are not executed",
            "Recorded relative times assert ordering; replay does not sleep at original wall-clock intervals",
        ],
        "deviceActions": False,
        "productionChanges": False,
    }
    (build / "result.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                key: report[key]
                for key in ("result", "nativeCases", "functionalCases", "failed", "skipped")
            }
        )
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
