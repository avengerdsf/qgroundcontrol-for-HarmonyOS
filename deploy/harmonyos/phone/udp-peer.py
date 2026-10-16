"""Simulated disarmed generic peer for the authorized phone's UDP diagnostics.

Run with the repository tmp/venv/Scripts/python.exe; pymavlink is already local.
Only 192.168.1.167:14550 is permitted. The Pad is never a destination.
All vehicle/parameter values are software fixtures. No flight commands are sent.
"""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import socket
import sys
import time

try:
    from pymavlink.dialects.v20 import common as mavlink
except ImportError:
    raise SystemExit("Use E:/tmp/QGC/tmp/venv/Scripts/python.exe; local pymavlink is required.")


PHONE = "192.168.1.167"
PHONE_PORT = 14550
LOCAL_PORT = 14750
ROOT = Path(__file__).resolve().parents[3]
VERIFICATION_ROOT = ROOT / "tmp/phone-v5.0.8/verification"
PARAMETERS = ((b"QGC_UDP_TEST", 1.0), (b"MPC_XY_CRUISE", 5.0))
OUTPUT_TYPES = {0: "HEARTBEAT", 22: "PARAM_VALUE", 44: "MISSION_COUNT",
                77: "COMMAND_ACK", 148: "AUTOPILOT_VERSION"}


def validate_options(target, phone_port, local_port, duration, report):
    if target != PHONE or phone_port != PHONE_PORT or local_port != LOCAL_PORT:
        raise ValueError(f"Only phone {PHONE}:{PHONE_PORT} from local port {LOCAL_PORT} is authorized.")
    if not math.isfinite(duration) or not 0 < duration <= 120:
        raise ValueError("Duration must be greater than zero and at most 120 seconds.")
    report = Path(report).resolve()
    if not report.is_relative_to(VERIFICATION_ROOT.resolve()) or report.suffix.lower() != ".json":
        raise ValueError(f"Report must be a new JSON file below {VERIFICATION_ROOT}.")
    if report.exists():
        raise ValueError("Report already exists; choose a new filename.")
    return report


class DiagnosticPeer:
    """MAVLink framing/fixtures only; network destinations are owned by main()."""

    def __init__(self):
        self.encoder = mavlink.MAVLink(None, srcSystem=42, srcComponent=1)
        self.decoder = mavlink.MAVLink(None)
        self.decoder.robust_parsing = True
        self.rx = Counter()
        self.tx = Counter()
        self.bad_frames = 0
        self.signed_frames_skipped = 0
        self.gcs_heartbeats = 0
        self.last_gcs_heartbeat = None
        self.parameter_requests = []

    def _pack(self, message):
        frame = message.pack(self.encoder, force_mavlink1=message.get_msgId() != 44)
        self.encoder.seq = (self.encoder.seq + 1) % 256
        return frame

    def heartbeat(self):
        return self._pack(self.encoder.heartbeat_encode(
            mavlink.MAV_TYPE_QUADROTOR, mavlink.MAV_AUTOPILOT_GENERIC,
            0, 0, mavlink.MAV_STATE_STANDBY))

    def _parameter(self, index):
        name, value = PARAMETERS[index]
        return self._pack(self.encoder.param_value_encode(
            name, value, mavlink.MAV_PARAM_TYPE_REAL32, len(PARAMETERS), index))

    def receive(self, data):
        responses = []
        for message in self.decoder.parse_buffer(data) or []:
            kind = message.get_type()
            if kind == "BAD_DATA":
                self.bad_frames += 1
                continue
            # Consume the signature trailer, but do not trust unauthenticated signed traffic.
            wire = message.get_msgbuf()
            if wire[0] == 0xFD and wire[2] & 1:
                self.signed_frames_skipped += 1
                continue
            self.rx[kind] += 1
            if kind == "HEARTBEAT" and message.type == mavlink.MAV_TYPE_GCS:
                self.gcs_heartbeats += 1
                self.last_gcs_heartbeat = {
                    "systemId": message.get_srcSystem(), "componentId": message.get_srcComponent(),
                    "type": message.type, "baseMode": message.base_mode,
                }
            if getattr(message, "target_system", 42) not in (0, 42) \
                    or getattr(message, "target_component", 1) not in (0, 1):
                continue
            if kind in ("PARAM_REQUEST_LIST", "PARAM_REQUEST_READ"):
                if len(self.parameter_requests) < 100:
                    self.parameter_requests.append({
                        "message": kind, "systemId": message.get_srcSystem(),
                        "componentId": message.get_srcComponent(),
                        "name": getattr(message, "param_id", ""),
                        "index": getattr(message, "param_index", None),
                    })
                if kind == "PARAM_REQUEST_LIST":
                    indices = range(len(PARAMETERS))
                elif message.param_index >= 0:
                    indices = [message.param_index] if message.param_index < len(PARAMETERS) else []
                else:
                    indices = [i for i, (name, _) in enumerate(PARAMETERS)
                               if name.decode("ascii") == message.param_id.rstrip("\0")]
                responses.extend(self._parameter(i) for i in indices)
            elif kind == "MISSION_REQUEST_LIST":
                responses.append(self._pack(self.encoder.mission_count_encode(
                    message.get_srcSystem(), message.get_srcComponent(), 0, message.mission_type)))
            elif kind == "COMMAND_LONG":
                version = message.command == mavlink.MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES \
                    or (message.command == mavlink.MAV_CMD_REQUEST_MESSAGE
                        and message.param1 == mavlink.MAVLINK_MSG_ID_AUTOPILOT_VERSION)
                result = mavlink.MAV_RESULT_ACCEPTED if version else mavlink.MAV_RESULT_UNSUPPORTED
                responses.append(self._pack(self.encoder.command_ack_encode(message.command, result)))
                if version:
                    responses.append(self._pack(self.encoder.autopilot_version_encode(
                        mavlink.MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT | mavlink.MAV_PROTOCOL_CAPABILITY_MAVLINK2,
                        0x010F00FF, 0, 0, 0, [0] * 8, [0] * 8, [0] * 8, 0, 0, 42)))
        return responses

    def record_sent(self, frame):
        message_id = int.from_bytes(frame[7:10], "little") if frame[0] == 0xFD else frame[5]
        self.tx[OUTPUT_TYPES[message_id]] += 1

    def evidence(self):
        return {
            "heartbeatExchange": self.tx["HEARTBEAT"] > 0 and self.gcs_heartbeats > 0,
            "simulatedParameterExchange": self.tx["PARAM_VALUE"] > 0
                and self.rx["PARAM_REQUEST_LIST"] + self.rx["PARAM_REQUEST_READ"] > 0,
            "rxMessages": dict(self.rx), "txMessages": dict(self.tx),
            "gcsHeartbeats": self.gcs_heartbeats, "lastGcsHeartbeat": self.last_gcs_heartbeat,
            "parameterRequests": self.parameter_requests, "badFrames": self.bad_frames,
            "signedFramesSkipped": self.signed_frames_skipped,
        }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=PHONE)
    parser.add_argument("--phone-port", type=int, default=PHONE_PORT)
    parser.add_argument("--local-port", type=int, default=LOCAL_PORT)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        report_path = validate_options(args.target, args.phone_port, args.local_port,
                                       args.duration, args.report)
    except ValueError as error:
        parser.error(str(error))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    protocol = DiagnosticPeer()
    started = datetime.now(timezone.utc).isoformat()
    ignored_senders = 0
    connection_resets = 0
    failure = None
    interrupted = False
    print(f"SIMULATED disarmed generic sys42/comp1 -> {PHONE}:{PHONE_PORT}; local UDP {LOCAL_PORT}", flush=True)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("0.0.0.0", LOCAL_PORT))
            udp.settimeout(0.2)
            deadline = time.monotonic() + args.duration
            next_heartbeat = time.monotonic()
            while time.monotonic() < deadline:
                if time.monotonic() >= next_heartbeat:
                    frame = protocol.heartbeat()
                    udp.sendto(frame, (PHONE, PHONE_PORT))
                    protocol.record_sent(frame)
                    next_heartbeat += 1
                try:
                    data, sender = udp.recvfrom(65535)
                except socket.timeout:
                    continue
                except ConnectionResetError as error:
                    if getattr(error, "winerror", None) != 10054:
                        raise
                    connection_resets += 1
                    continue
                if sender != (PHONE, PHONE_PORT):
                    ignored_senders += 1
                    continue
                before = protocol.rx.copy()
                responses = protocol.receive(data)
                for name in ("HEARTBEAT", "PARAM_REQUEST_LIST", "PARAM_REQUEST_READ"):
                    if protocol.rx[name] != before[name]:
                        print(f"RX {name}: {protocol.rx[name]}", flush=True)
                for frame in responses:
                    udp.sendto(frame, (PHONE, PHONE_PORT))
                    protocol.record_sent(frame)
                    if frame[5] == 22:
                        print(f"TX PARAM_VALUE: {protocol.tx['PARAM_VALUE']}", flush=True)
    except KeyboardInterrupt:
        interrupted = True
    except (OSError, mavlink.MAVError) as error:
        failure = str(error)
    evidence = protocol.evidence()
    report = {
        "schemaVersion": 1, "scope": "Simulated software flight-controller UDP/MAVLink diagnostic",
        "physicalFlightControllerTested": False, "simulatedSystemId": 42, "simulatedComponentId": 1,
        "simulatedAutopilot": "generic",
        "simulatedArmed": False, "target": PHONE, "phonePort": PHONE_PORT, "localPort": LOCAL_PORT,
        "startedUtc": started, "finishedUtc": datetime.now(timezone.utc).isoformat(),
        "durationRequestedSeconds": args.duration, "interrupted": interrupted, "failure": failure,
        "ignoredSenders": ignored_senders, "connectionResetEvents": connection_resets,
        "parameters": {name.decode(): value for name, value in PARAMETERS},
        **evidence,
    }
    report["status"] = "error" if failure else "interrupted" if interrupted else \
        "simulated-parameter-exchange" if evidence["simulatedParameterExchange"] \
        and evidence["heartbeatExchange"] else "heartbeat-exchange" if evidence["heartbeatExchange"] \
        else "no-bidirectional-proof"
    with report_path.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, ensure_ascii=False)
        output.write("\n")
    print(f"{report['status']}: {report_path}", flush=True)
    return 130 if interrupted else 1 if failure else 0 if evidence["heartbeatExchange"] else 2


if __name__ == "__main__":
    sys.exit(main())
