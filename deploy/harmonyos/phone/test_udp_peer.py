"""Offline/localhost checks; these tests never address a phone or the Pad."""

import importlib.util
import socket
import subprocess
import sys
import unittest
from pathlib import Path

from pymavlink.dialects.v20 import common as mavlink

SCRIPT = Path(__file__).with_name("udp-peer.py")
ROOT = Path(__file__).resolve().parents[3]
REPORT = ROOT / "tmp/phone-v5.0.8/verification/udp-peer-unit-test.json"
peer = None
if SCRIPT.exists():
    spec = importlib.util.spec_from_file_location("udp_peer", SCRIPT)
    peer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(peer)


def decode(data):
    parser = mavlink.MAVLink(None)
    return parser.parse_buffer(data) or []


class UdpPeerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if peer is None:
            raise AssertionError("udp-peer.py protocol implementation is missing")

    def setUp(self):
        self.protocol = peer.DiagnosticPeer()
        self.gcs = mavlink.MAVLink(None, srcSystem=255, srcComponent=190)

    def request(self, message):
        return self.protocol.receive(message.pack(self.gcs))

    def test_disarmed_heartbeat_matches_mavlink1_crc_vector(self):
        frame = self.protocol.heartbeat()
        self.assertEqual(frame.hex(), "fe09002a01000000000002000003032613")
        heartbeat = decode(frame)[0]
        self.assertEqual((heartbeat.get_srcSystem(), heartbeat.get_srcComponent()), (42, 1))
        self.assertEqual(heartbeat.base_mode & mavlink.MAV_MODE_FLAG_SAFETY_ARMED, 0)
        self.assertEqual(heartbeat.autopilot, mavlink.MAV_AUTOPILOT_GENERIC)
        self.assertEqual(self.protocol.heartbeat()[2], 1)

    def test_empty_mission_lists_preserve_type_and_requester(self):
        for mission_type in (
            mavlink.MAV_MISSION_TYPE_MISSION,
            mavlink.MAV_MISSION_TYPE_FENCE,
            mavlink.MAV_MISSION_TYPE_RALLY,
        ):
            with self.subTest(mission_type=mission_type):
                responses = self.request(self.gcs.mission_request_list_encode(42, 1, mission_type))
                self.assertEqual(len(responses), 1)
                self.assertEqual(responses[0][0], 0xFD)
                response = decode(responses[0])[0]
                self.assertEqual(response.get_type(), "MISSION_COUNT")
                self.assertEqual(
                    (
                        response.target_system,
                        response.target_component,
                        response.count,
                        response.mission_type,
                    ),
                    (255, 190, 0, mission_type),
                )
                self.protocol.record_sent(responses[0])
        self.assertEqual(self.protocol.tx["MISSION_COUNT"], 3)
        self.assertEqual(self.request(self.gcs.mission_request_list_encode(43, 1, 0)), [])

    def test_v2_crc_corruption_is_rejected(self):
        frame = bytearray(self.gcs.heartbeat_encode(6, 8, 0, 0, 4).pack(self.gcs))
        frame[-1] ^= 1
        self.assertEqual(self.protocol.receive(bytes(frame)), [])
        self.assertEqual(self.protocol.rx.get("HEARTBEAT", 0), 0)
        self.assertGreater(self.protocol.bad_frames, 0)

    def test_v2_24bit_message_and_signed_frame_do_not_hide_next_heartbeat(self):
        wide_id = self.gcs.button_change_encode(1, 2, 3).pack(self.gcs)
        self.gcs.signing.secret_key = b"x" * 32
        self.gcs.signing.sign_outgoing = True
        signed = self.gcs.heartbeat_encode(6, 8, 0, 0, 4).pack(self.gcs)
        self.gcs.signing.sign_outgoing = False
        heartbeat = self.gcs.heartbeat_encode(6, 8, 0, 0, 4).pack(self.gcs)
        self.assertEqual(self.protocol.receive(wide_id + signed + heartbeat), [])
        self.assertEqual(self.protocol.rx["BUTTON_CHANGE"], 1)
        self.assertEqual(self.protocol.rx["HEARTBEAT"], 1)
        self.assertEqual(self.protocol.signed_frames_skipped, 1)

    def test_parameter_list_and_reads_return_correct_fixture_values(self):
        values = decode(b"".join(self.request(self.gcs.param_request_list_encode(42, 1))))
        self.assertEqual(
            [(m.param_id, m.param_value, m.param_count, m.param_index) for m in values],
            [("QGC_UDP_TEST", 1.0, 2, 0), ("MPC_XY_CRUISE", 5.0, 2, 1)],
        )
        for name, index in [(b"MPC_XY_CRUISE", -1), (b"", 1)]:
            value = decode(
                b"".join(self.request(self.gcs.param_request_read_encode(42, 1, name, index)))
            )[0]
            self.assertEqual((value.param_id, value.param_value), ("MPC_XY_CRUISE", 5.0))
        self.assertEqual(self.request(self.gcs.param_request_list_encode(43, 1)), [])
        self.assertEqual(self.request(self.gcs.param_request_read_encode(42, 1, b"", 99)), [])

    def test_version_request_responds_without_generating_control_commands(self):
        request = self.gcs.command_long_encode(42, 1, 512, 0, 148, 0, 0, 0, 0, 0, 0)
        messages = decode(b"".join(self.request(request)))
        self.assertEqual([m.get_type() for m in messages], ["COMMAND_ACK", "AUTOPILOT_VERSION"])
        self.assertEqual(messages[0].result, mavlink.MAV_RESULT_ACCEPTED)
        arm = self.gcs.command_long_encode(42, 1, 400, 0, 1, 0, 0, 0, 0, 0, 0)
        messages = decode(b"".join(self.request(arm)))
        self.assertEqual([m.get_type() for m in messages], ["COMMAND_ACK"])
        self.assertEqual(messages[0].result, mavlink.MAV_RESULT_UNSUPPORTED)
        self.assertEqual(decode(self.protocol.heartbeat())[0].base_mode, 0)

    def test_real_localhost_heartbeat_and_parameter_roundtrip(self):
        with (
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as simulated,
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as ground,
        ):
            simulated.bind(("127.0.0.1", 0))
            ground.bind(("127.0.0.1", 0))
            simulated.settimeout(2)
            ground.settimeout(2)
            heartbeat = self.protocol.heartbeat()
            simulated.sendto(heartbeat, ground.getsockname())
            self.protocol.record_sent(heartbeat)
            self.assertEqual(decode(ground.recv(2048))[0].get_type(), "HEARTBEAT")
            ground.sendto(
                self.gcs.heartbeat_encode(6, 8, 0, 0, 4).pack(self.gcs)
                + self.gcs.param_request_list_encode(42, 1).pack(self.gcs),
                simulated.getsockname(),
            )
            responses = self.protocol.receive(simulated.recv(2048))
            for frame in responses:
                simulated.sendto(frame, ground.getsockname())
                self.protocol.record_sent(frame)
            self.assertEqual(
                [decode(ground.recv(2048))[0].param_id for _ in responses],
                ["QGC_UDP_TEST", "MPC_XY_CRUISE"],
            )
        evidence = self.protocol.evidence()
        self.assertTrue(evidence["heartbeatExchange"])
        self.assertTrue(evidence["simulatedParameterExchange"])
        self.assertEqual(evidence["txMessages"], {"HEARTBEAT": 1, "PARAM_VALUE": 2})

    def test_strict_target_ports_duration_and_report_guards(self):
        peer.validate_options("192.168.1.167", 14550, 14750, 60, REPORT)
        for address in ["192.168.1.221", "255.255.255.255", "224.0.0.1", "127.0.0.1", "localhost"]:
            with self.subTest(address=address), self.assertRaises(ValueError):
                peer.validate_options(address, 14550, 14750, 60, REPORT)
        for phone_port, local_port, duration in [
            (14551, 14750, 60),
            (14550, 0, 60),
            (14550, 14750, 0),
            (14550, 14750, float("nan")),
            (14550, 14750, 121),
        ]:
            with self.assertRaises(ValueError):
                peer.validate_options("192.168.1.167", phone_port, local_port, duration, REPORT)
        for path in [ROOT / "outside.json", SCRIPT, REPORT.with_suffix(".txt")]:
            with self.assertRaises(ValueError):
                peer.validate_options("192.168.1.167", 14550, 14750, 60, path)

    def test_cli_rejects_pad_before_opening_network_or_creating_report(self):
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT),
                "--target",
                "192.168.1.221",
                "--report",
                str(REPORT),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("192.168.1.167", result.stderr)
        self.assertFalse(REPORT.exists())


if __name__ == "__main__":
    unittest.main()
