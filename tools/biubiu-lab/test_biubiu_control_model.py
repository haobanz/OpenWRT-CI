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
        self.assertEqual(request.body["data"]["engineClient"], engine_client)
        self.assertEqual(request.body["data"]["list"][0]["port"], 443)

        with self.assertRaises(ValueError):
            control.signal_login_request(
                self.identity,
                self.client,
                engine_client,
                [],
                "private-signal-ticket",
            )


if __name__ == "__main__":
    unittest.main()
