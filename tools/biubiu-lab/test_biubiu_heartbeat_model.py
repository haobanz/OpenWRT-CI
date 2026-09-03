#!/usr/bin/env python3

import json
import unittest

import biubiu_heartbeat_model as heartbeat


class HeartbeatModelTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = heartbeat.build_engine_client(
            version_name="4.2.1",
            version_code=40201,
            channel="clean-room-test",
            network="ETHERNET",
            api_level=35,
            build_code="test-build",
        )
        self.material = heartbeat.HeartbeatCipherMaterial(
            key=b"0123456789abcdef",
            iv=b"abcdef0123456789",
        )

    def test_engine_client_matches_observed_shape(self) -> None:
        self.assertEqual(
            json.loads(self.client),
            {
                "ver": "4.2.1",
                "versionCode": 40201,
                "os": "android",
                "ch": "clean-room-test",
                "network": "ETHERNET",
                "apiLevel": 35,
                "build": "test-build",
            },
        )
        self.assertNotIn(" ", self.client)

    def test_plaintext_maps_console_type_and_string_ids(self) -> None:
        console = json.loads(
            heartbeat.build_heartbeat_plaintext(
                uid=123456,
                signal_session_id="synthetic-signal-session",
                platform_id=7,
                game_id=730,
                area_id=1,
                engine_version="3.0.0",
                engine_client=self.client,
            )
        )
        desktop = json.loads(
            heartbeat.build_heartbeat_plaintext(
                uid=123456,
                signal_session_id="synthetic-signal-session",
                platform_id=6,
                game_id=730,
                area_id=0,
                engine_version="3.0.0",
                engine_client=self.client,
            )
        )

        self.assertEqual(console["type"], 5)
        self.assertEqual(desktop["type"], 1)
        self.assertEqual(console["uid"], 123456)
        self.assertEqual(console["gameId"], "730")
        self.assertEqual(console["areaId"], "1")
        self.assertEqual(console["engineClient"], self.client)

    def test_cipher_round_trip_and_request_shape(self) -> None:
        session_marker = "synthetic-private-session"
        plaintext = heartbeat.build_heartbeat_plaintext(
            uid=123456,
            signal_session_id=session_marker,
            platform_id=6,
            game_id=730,
            area_id=1,
            engine_version="3.0.0",
            engine_client=self.client,
        )
        encrypted = heartbeat.encrypt_heartbeat_payload(plaintext, self.material)
        request = heartbeat.build_heartbeat_request(
            "gtm-signal.example.test", encrypted, self.client
        )

        self.assertTrue(encrypted.endswith("\n"))
        self.assertEqual(
            heartbeat.decrypt_heartbeat_payload(encrypted, self.material), plaintext
        )
        self.assertEqual(request.method, "POST")
        self.assertEqual(
            request.url,
            "https://gtm-signal.example.test/api/open.heartbeat.heartbeatV2",
        )
        self.assertEqual(request.headers["Content-Type"], "application/json")
        self.assertEqual(request.headers["x-biu-client"], self.client)
        self.assertEqual(json.loads(request.body)["caller"], "ping_android")
        self.assertEqual(json.loads(request.body)["data"], encrypted)
        self.assertNotIn(session_marker, repr(request))
        self.assertNotIn(encrypted.strip(), repr(request))

    def test_success_response_exposes_typed_endpoints(self) -> None:
        response = {
            "code": heartbeat.HEARTBEAT_SUCCESS_CODE,
            "msg": "ok",
            "data": {
                "state": 1,
                "dataChannelList": [
                    {"proType": "udp", "channelIp": "192.0.2.7", "port": 443},
                    {"proType": "TCP", "channelIp": "198.51.100.9", "port": 80},
                ],
            },
        }
        encrypted = heartbeat.encrypt_heartbeat_payload(
            json.dumps(response, separators=(",", ":")).encode(), self.material
        )
        result = heartbeat.parse_encrypted_heartbeat_response(
            encrypted, self.material
        )

        self.assertTrue(result.successful)
        self.assertEqual(result.state, 1)
        self.assertEqual(
            result.endpoints,
            ("UDP://192.0.2.7:443", "TCP://198.51.100.9:80"),
        )
        self.assertNotIn("ok", repr(result))

    def test_service_error_does_not_require_data(self) -> None:
        result = heartbeat.parse_heartbeat_response(
            {"code": 400001, "msg": "synthetic rejection"}
        )
        self.assertFalse(result.successful)
        self.assertIsNone(result.state)
        self.assertEqual(result.channels, ())
        self.assertNotIn("synthetic rejection", repr(result))

    def test_success_requires_state_and_valid_channels(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing state"):
            heartbeat.parse_heartbeat_response(
                {"code": heartbeat.HEARTBEAT_SUCCESS_CODE, "msg": "ok", "data": {}}
            )
        invalid_channels = (
            {"proType": "SCTP", "channelIp": "192.0.2.1", "port": 443},
            {"proType": "TCP", "channelIp": "2001:db8::1", "port": 443},
            {"proType": "TCP", "channelIp": "192.0.2.1", "port": 0},
        )
        for channel in invalid_channels:
            with self.subTest(channel=channel), self.assertRaises(ValueError):
                heartbeat.parse_heartbeat_response(
                    {
                        "code": heartbeat.HEARTBEAT_SUCCESS_CODE,
                        "msg": "ok",
                        "data": {"state": 1, "dataChannelList": [channel]},
                    }
                )

    def test_invalid_cipher_and_destination_inputs_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            heartbeat.HeartbeatCipherMaterial(b"short", b"abcdef0123456789")
        with self.assertRaises(ValueError):
            heartbeat.decrypt_heartbeat_payload("not-base64", self.material)

        valid_ciphertext = heartbeat.encrypt_heartbeat_payload(b"{}", self.material)
        for host in (
            "user@example.test",
            "example.test/api",
            "example.test?query=1",
        ):
            with self.subTest(host=host), self.assertRaises(ValueError):
                heartbeat.build_heartbeat_request(
                    host, valid_ciphertext, self.client
                )
        with self.assertRaises(ValueError):
            heartbeat.build_heartbeat_request(
                "example.test", valid_ciphertext, self.client, scheme="ftp"
            )


if __name__ == "__main__":
    unittest.main()
