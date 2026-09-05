#!/usr/bin/env python3

import copy
import json
import unittest

import biubiu_control_model as control


class ControlModelTests(unittest.TestCase):
    def setUp(self) -> None:
        self.record = {
            "login": {
                "data": {
                    "userBasicInfo": {"localId": "123456789"},
                    "sessionInfo": {"sessionId": "private-service-ticket"},
                }
            }
        }
        self.identity = control.AccountIdentity.from_session_record(self.record)
        self.client = {
            "appId": "offline-app",
            "deviceId": "offline-device",
            "deviceIdType": "utdid",
            "hostAppId": "offline-app",
            "ex": {"os": "openwrt", "ver": "0.1"},
        }

    def test_session_identity_is_validated(self) -> None:
        self.assertEqual(self.identity.uid, "123456789")
        self.assertEqual(self.identity.service_ticket, "private-service-ticket")
        self.assertNotIn("private-service-ticket", repr(self.identity))
        for local_id in ("", "abc", "0", 123):
            invalid = copy.deepcopy(self.record)
            invalid["login"]["data"]["userBasicInfo"]["localId"] = local_id
            with self.subTest(local_id=local_id), self.assertRaises(ValueError):
                control.AccountIdentity.from_session_record(invalid)

    def test_game_list_injects_identity_without_mutating_template(self) -> None:
        original = copy.deepcopy(self.client)
        request = control.game_list_request(
            self.identity,
            self.client,
            page=2,
            size=50,
            last_sort_key="next",
            request_id="17000000000001234",
        )

        self.assertEqual(request.endpoint, control.GAME_LIST_ENDPOINT)
        self.assertEqual(
            request.body["data"],
            {"page": {"page": 2, "size": 50}, "lastSortKey": "next"},
        )
        wire_client = json.loads(request.body["client"])
        self.assertEqual(wire_client["ex"]["st"], "private-service-ticket")
        self.assertEqual(wire_client["ex"]["biuid"], "123456789")
        self.assertEqual(self.client, original)
        self.assertNotIn("private-service-ticket", repr(request))

    def test_conflicting_identity_is_rejected(self) -> None:
        client = copy.deepcopy(self.client)
        client["ex"]["biuid"] = "987654321"
        with self.assertRaises(ValueError):
            control.game_list_request(self.identity, client)

    def test_search_and_entitlement_shapes(self) -> None:
        search = control.search_game_request(
            self.identity,
            self.client,
            "Counter-Strike",
            request_id="17000000000001234",
        )
        self.assertEqual(search.endpoint, control.SEARCH_GAME_ENDPOINT)
        self.assertEqual(search.body["data"]["keyword"], "Counter-Strike")

        entitlement = control.check_speedup_request(
            self.identity,
            self.client,
            10,
            20,
            polling=1,
            request_id="17000000000001234",
        )
        self.assertEqual(entitlement.endpoint, control.CHECK_SPEEDUP_ENDPOINT)
        self.assertEqual(
            entitlement.body["data"],
            {"gameId": 10, "areaId": 20, "polling": 1, "space": 0},
        )

    def test_native_entitlement_omits_cold_start_jitter(self) -> None:
        request = control.pc_check_speedup_request(
            self.identity,
            self.client,
            10,
            20,
            request_id="17000000000001234",
        )

        self.assertEqual(request.endpoint, control.PC_CHECK_SPEEDUP_ENDPOINT)
        self.assertNotIn("df=adat", request.endpoint)
        self.assertEqual(
            request.body["data"],
            {
                "gameId": 10,
                "areaId": 20,
                "polling": 0,
                "useMemberSpeedUpExperience": False,
            },
        )

        warm_request = control.pc_check_speedup_request(
            self.identity,
            self.client,
            10,
            20,
            polling=1,
            use_member_speedup_experience=True,
            last_jitter_time=1700000000000,
            request_id="17000000000001234",
        )
        self.assertEqual(warm_request.body["data"]["lastJitterTime"], 1700000000000)
        with self.assertRaises(ValueError):
            control.pc_check_speedup_request(
                self.identity,
                self.client,
                10,
                20,
                last_jitter_time="undefined",  # type: ignore[arg-type]
            )

    def test_native_game_map_and_start_metadata_match_windows_client(self) -> None:
        request = control.pc_game_map_request(
            self.identity,
            self.client,
            [38780],
            request_id="17000000000001234",
        )
        self.assertEqual(request.endpoint, control.PC_GAME_MAP_ENDPOINT)
        self.assertEqual(request.body["data"], {"gameIds": [38780]})
        self.assertNotIn("df=adat", request.endpoint)

        game = {
            "gameInfo": {"gameId": 38780, "platformId": 6},
            "areaList": [{"areaId": 146}, {"areaId": 103}],
            "speedupModelList": [
                {"speedupModelId": 3},
                {"speedupModelId": 5},
            ],
        }
        metadata = control.pc_start_metadata(
            game, 38780, 146, selected_mode=5
        )
        self.assertEqual(
            metadata,
            {
                "gameId": 38780,
                "gameArea": 146,
                "serverId": 0,
                "accMode": 5,
                "accModeList": [5, 3],
                "accPodId": "auto",
                "gamePlatform": "pc",
                "gamePlatformId": 6,
            },
        )
        self.assertEqual(control.PC_PLATFORM_NAMES[control.Platform.XBOX], "xbox")
        self.assertEqual(
            control.PC_PLATFORM_NAMES[control.Platform.PLAYSTATION], "ps"
        )

    def test_native_start_metadata_fallback_and_mismatch_rejection(self) -> None:
        game = {
            "gameInfo": {"gameId": 38780, "platformId": 6},
            "areaList": [{"areaId": 146}],
        }
        metadata = control.pc_start_metadata(game, 38780, 146)
        self.assertEqual(metadata["accMode"], 3)
        self.assertEqual(metadata["accModeList"], [3, 5])

        with self.assertRaises(ValueError):
            control.pc_start_metadata(game, 38780, 999)
        with self.assertRaises(ValueError):
            control.pc_start_metadata(game, 38780, 146, selected_mode=4)
        invalid_platform = copy.deepcopy(game)
        invalid_platform["gameInfo"]["platformId"] = 11
        with self.assertRaises(ValueError):
            control.pc_start_metadata(invalid_platform, 38780, 146)

    def test_speedup_config_keeps_platform_and_package_data(self) -> None:
        package_request = {
            "gamePackageInfo": {
                "appName": "",
                "packageName": "",
                "versionName": "",
                "versionCode": "",
            },
            "signCheckPackageList": [],
            "sourcePkgList": [],
        }
        request = control.speedup_config_request(
            self.identity,
            self.client,
            10,
            20,
            control.Platform.PC,
            package_request=package_request,
            request_id="17000000000001234",
        )
        self.assertEqual(request.endpoint, control.SPEEDUP_CONFIG_ENDPOINT)
        self.assertEqual(request.body["data"]["platformId"], 6)
        self.assertEqual(request.body["data"]["clientSessionId"], "")
        self.assertEqual(
            request.body["data"]["scoutPathResult"],
            {"strategyId": "", "detectResult": []},
        )
        self.assertEqual(request.body["data"]["optimizeMode"], 0)
        self.assertEqual(request.body["data"]["dualNetOnline"], 0)
        self.assertEqual(request.body["data"]["pkgRequest"], package_request)

    def test_signal_login_shape_and_validation(self) -> None:
        engine_client = {
            "appId": "offline-app",
            "areaId": 20,
            "engineVersion": "offline",
            "gameId": 10,
            "signalSessionId": "",
            "type": 6,
            "uid": "123456789",
        }
        request = control.signal_login_request(
            self.identity,
            self.client,
            engine_client,
            [{"dataChannelIp": "192.0.2.1", "port": 443, "proType": "tcp"}],
            "private-signal-ticket",
            request_id="17000000000001234",
        )
        self.assertEqual(request.endpoint, control.SIGNAL_LOGIN_ENDPOINT)
        self.assertEqual(
            request.endpoint,
            "/api/ping-signal.open.login.loginV2?ver=1.0.0&df=adat",
        )
        self.assertEqual(request.body["data"]["engineClient"], engine_client)
        self.assertEqual(request.body["data"]["list"][0]["port"], 443)
        self.assertEqual(request.body["data"]["list"][0]["proType"], "TCP")

        with self.assertRaises(ValueError):
            control.signal_login_request(
                self.identity,
                self.client,
                engine_client,
                [],
                "private-signal-ticket",
            )

        self.assertEqual(
            control.CHANNEL_TICKET_ENDPOINT,
            "/api/ping-signal.open.auth.getChannelStV2?ver=1.0.0&df=adat",
        )

    def test_native_pc_signal_login_matches_windows_serializer(self) -> None:
        engine_client = {
            "appId": "biubiu",
            "engineVersion": "1.0.0.0",
            "gameId": 38780,
            "areaId": 146,
            "serverId": 0,
            "signalSessionId": "",
            "type": 4,
            "uid": "123456789",
        }
        channels = [
            {
                "dataChannelIp": "192.0.2.20",
                "port": 443,
                "proType": "TCP",
            },
            {
                "dataChannelIp": "192.0.2.21",
                "port": 8000,
                "proType": "UDP",
            },
        ]
        request = control.pc_signal_login_request(
            self.identity,
            self.client,
            engine_client,
            channels,
            "private-signal-ticket",
        )

        self.assertEqual(list(request.body), ["client", "data", "id"])
        self.assertEqual(
            list(request.body["data"]), ["signalSt", "engineClient", "list"]
        )
        self.assertEqual(request.body["data"]["engineClient"], engine_client)
        self.assertEqual(request.body["data"]["list"], channels)
        self.assertEqual(request.body["data"]["engineClient"]["type"], 4)
        self.assertEqual(request.body["id"], "")
        self.assertEqual(
            json.loads(request.body["client"])["ex"]["biuid"],
            self.identity.uid,
        )

        with self.assertRaises(ValueError):
            control.pc_signal_login_request(
                self.identity,
                self.client,
                engine_client,
                channels,
                "",
            )
        with self.assertRaises(ValueError):
            control.pc_signal_login_request(
                self.identity,
                self.client,
                engine_client,
                [],
                "private-signal-ticket",
            )

    def test_signal_authorization_is_validated_and_redacted(self) -> None:
        response_data = {
            "signalSessionId": "private-signal-session",
            "token": "cHJpdmF0ZS1zaWduYWwtdG9rZW4=",
            "xor": "1",
            "channelAuthList": [
                {
                    "channelAddress": "test-channel",
                    "channelIp": "192.0.2.20",
                    "channelSt": "private-channel-ticket",
                    "dataChannelSessionId": 0x01020304,
                    "expireTime": 1700000000000,
                    "port": 443,
                    "proType": "tcp",
                    "secretType": "private-secret-type",
                }
            ],
        }
        authorization = control.parse_signal_authorization(response_data)

        self.assertEqual(authorization.channels[0].protocol, "TCP")
        self.assertEqual(authorization.channels[0].engine_protocol_id, 6)
        self.assertEqual(authorization.channels[0].channel_ip, "192.0.2.20")
        self.assertEqual(
            authorization.summary(),
            {
                "channelCount": 1,
                "protocols": ["TCP"],
                "hasSignalSession": True,
                "hasToken": True,
                "hasXor": True,
            },
        )
        rendered = repr(authorization)
        self.assertNotIn("private-signal-session", rendered)
        self.assertNotIn("cHJpdmF0ZS1zaWduYWwtdG9rZW4=", rendered)
        self.assertNotIn("private-channel-ticket", rendered)
        self.assertNotIn("private-secret-type", rendered)

        renewed = control.parse_channel_ticket_data(
            {"dataChannelList": response_data["channelAuthList"]}
        )
        self.assertEqual(renewed, authorization.channels)

        nullable = copy.deepcopy(response_data)
        nullable["xor"] = None
        nullable["channelAuthList"][0]["channelAddress"] = None
        parsed_nullable = control.parse_signal_authorization(nullable)
        self.assertEqual(parsed_nullable.xor, "")
        self.assertEqual(parsed_nullable.channels[0].channel_address, "")

    def test_signal_authorization_builds_redacted_bproxy_handoff(self) -> None:
        response_data = {
            "signalSessionId": "private-signal-session",
            "token": "cHJpdmF0ZS10b2tlbg==",
            "xor": "true",
            "channelAuthList": [
                {
                    "channelAddress": "udp-channel",
                    "channelIp": "192.0.2.21",
                    "channelSt": "private-udp-ticket",
                    "dataChannelSessionId": 17,
                    "expireTime": 1700000000000,
                    "port": 443,
                    "proType": "UDP",
                    "secretType": "private-udp-secret",
                },
                {
                    "channelAddress": "tcp-channel",
                    "channelIp": "192.0.2.22",
                    "channelSt": "private-tcp-ticket",
                    "dataChannelSessionId": 6,
                    "expireTime": 1700000000000,
                    "port": 443,
                    "proType": "TCP",
                    "secretType": "private-tcp-secret",
                },
            ],
        }
        authorization = control.parse_signal_authorization(response_data)
        bproxy = authorization.bproxy_authorization()

        self.assertEqual(
            [channel.engine_protocol_id for channel in authorization.channels],
            [0x11, 6],
        )
        self.assertEqual(bproxy.token, b"private-token")
        self.assertEqual(bproxy.xor_marker, ord("t"))
        self.assertEqual(
            bproxy.summary(),
            {"hasToken": True, "hasXorMarker": True, "hasTcpSession": True},
        )
        self.assertNotIn("private-token", repr(bproxy))
        self.assertNotIn("tcp_session_id=6", repr(bproxy))

        udp_only = copy.deepcopy(response_data)
        udp_only["channelAuthList"] = udp_only["channelAuthList"][:1]
        self.assertEqual(
            control.parse_signal_authorization(udp_only)
            .bproxy_authorization()
            .tcp_session_id,
            0,
        )

        invalid_token = copy.deepcopy(response_data)
        invalid_token["token"] = "not Base64!"
        with self.assertRaises(ValueError):
            control.parse_signal_authorization(
                invalid_token
            ).bproxy_authorization()

    def test_channel_ticket_request_matches_observed_shape(self) -> None:
        engine_client = {
            "gameId": 10,
            "uid": "123456789",
            "type": 6,
            "areaId": 20,
            "engineVersion": "offline",
            "appId": "offline-app",
            "signalSessionId": "private-signal-session",
            "speedupSession": "private-speedup-session",
        }
        channel = {
            "channelIp": "192.0.2.20",
            "dataChannelSessionId": 0x01020304,
            "port": 443,
            "proType": "tcp",
            "secretType": "offline-secret-type",
            "type": 1,
        }
        request = control.channel_ticket_request(
            self.identity,
            self.client,
            engine_client,
            [channel],
            request_id="17000000000001234",
        )

        self.assertEqual(request.endpoint, control.CHANNEL_TICKET_ENDPOINT)
        self.assertEqual(request.body["data"]["engineClient"], engine_client)
        wire_channel = request.body["data"]["channelAuthDTO"]["dataChannelList"][0]
        self.assertEqual(wire_channel["proType"], "TCP")
        self.assertNotIn("channelSt", wire_channel)
        self.assertNotIn("private-signal-session", repr(request))

    def test_invalid_signal_authorization_is_rejected(self) -> None:
        base = {
            "signalSessionId": "session",
            "token": "token",
            "xor": "",
            "channelAuthList": [
                {
                    "channelAddress": "",
                    "channelIp": "192.0.2.20",
                    "channelSt": "ticket",
                    "dataChannelSessionId": 1,
                    "expireTime": 1,
                    "port": 443,
                    "proType": "UDP",
                    "secretType": "type",
                }
            ],
        }
        invalid_values = (None, {}, {**base, "channelAuthList": []})
        for value in invalid_values:
            with self.subTest(value=value), self.assertRaises(ValueError):
                control.parse_signal_authorization(value)

        for field, value in (
            ("channelIp", "not-an-ip"),
            ("dataChannelSessionId", 0),
            ("port", 0),
            ("proType", "unknown"),
            ("channelSt", ""),
        ):
            invalid = copy.deepcopy(base)
            invalid["channelAuthList"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                control.parse_signal_authorization(invalid)


if __name__ == "__main__":
    unittest.main()
