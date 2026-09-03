#!/usr/bin/env python3

import copy
import json
import unittest

import biubiu_profile_model as profile_model


def valid_profile() -> dict:
    bolt_detail = {
        "detectIp": "198.51.100.7",
        "dataChannelList": [
            {
                "proType": "TCP",
                "ip": "192.0.2.10",
                "port": 443,
                "isp": 1,
                "encryption": True,
                "bbCliParam": "private-client-parameter",
                "bbSrvParam": "private-server-parameter",
                "bbstrategy": "private-strategy",
            },
            {
                "proType": "UDP",
                "ip": "192.0.2.11",
                "port": 5000,
                "bip": "192.0.2.12",
                "bisp": 2,
                "encryption": False,
            },
        ],
    }
    return {
        "inboundProfile": {
            "allowedAppList": [],
            "dnsServerList": ["8.8.8.8"],
            "searchDomainList": [],
            "localIpList": ["10.222.0.1/16"],
            "includeRouteList": ["203.0.113.0/24"],
            "excludeRouteList": ["192.168.0.0/16"],
            "mtu": 1400,
        },
        "outboundProfile": {
            "outboundConfigList": [
                {"id": "direct", "type": "direct", "name": "Direct"},
                {
                    "id": "accelerated",
                    "type": "bolt",
                    "name": "Test node",
                    "rawDetailConfig": json.dumps(bolt_detail),
                },
            ],
            "signalConfig": {
                "signalSt": "private-signal-ticket",
                "heartbeatInterval": 15,
                "heartbeatMaxInterval": 60,
                "maxReGetStCount": 3,
            },
        },
        "routerProfile": {
            "defaultOutboundId": "direct",
            "routeList": [
                {
                    "id": 7,
                    "mode": "bolt",
                    "cidrList": ["203.0.113.5/24"],
                    "outboundId": "accelerated",
                    "protocol": 17,
                    "portList": ["27000-27100", 443],
                }
            ],
        },
    }


class ProfileModelTests(unittest.TestCase):
    def test_profile_is_parsed_and_normalized(self) -> None:
        profile = profile_model.parse_acceleration_profile(valid_profile())

        self.assertEqual(profile.tun.local_networks, ("10.222.0.0/16",))
        self.assertEqual(profile.routes[0].selectors, ("203.0.113.0/24",))
        self.assertEqual(profile.routes[0].ports, ("27000-27100", "443"))
        self.assertEqual(
            [channel.protocol for channel in profile.outbounds[1].channels],
            ["TCP", "UDP"],
        )
        self.assertEqual(
            profile.summary(),
            {
                "outboundCount": 2,
                "routeCount": 1,
                "channelProtocols": ["TCP", "UDP"],
                "hasSignalTicket": True,
                "includeNetworkCount": 1,
                "excludeNetworkCount": 1,
            },
        )

    def test_sensitive_values_are_not_in_repr(self) -> None:
        profile = profile_model.parse_acceleration_profile(valid_profile())
        rendered = repr(profile)

        self.assertNotIn("private-signal-ticket", rendered)
        self.assertNotIn("private-client-parameter", rendered)
        self.assertNotIn("private-server-parameter", rendered)
        self.assertNotIn("private-strategy", rendered)

    def test_json_document_is_supported(self) -> None:
        parsed = profile_model.parse_acceleration_profile(json.dumps(valid_profile()))
        self.assertEqual(parsed.default_outbound_id, "direct")

    def test_unknown_route_outbound_is_rejected(self) -> None:
        document = valid_profile()
        document["routerProfile"]["routeList"][0]["outboundId"] = "missing"
        with self.assertRaisesRegex(ValueError, "unknown outbound"):
            profile_model.parse_acceleration_profile(document)

    def test_duplicate_channel_protocol_is_rejected(self) -> None:
        document = valid_profile()
        raw = json.loads(
            document["outboundProfile"]["outboundConfigList"][1]["rawDetailConfig"]
        )
        raw["dataChannelList"][1]["proType"] = "tcp"
        document["outboundProfile"]["outboundConfigList"][1]["rawDetailConfig"] = raw
        with self.assertRaisesRegex(ValueError, "duplicate"):
            profile_model.parse_acceleration_profile(document)

    def test_bad_network_and_port_are_rejected(self) -> None:
        bad_network = valid_profile()
        bad_network["inboundProfile"]["includeRouteList"] = ["not-a-network"]
        with self.assertRaisesRegex(ValueError, "IP network"):
            profile_model.parse_acceleration_profile(bad_network)

        bad_port = valid_profile()
        bad_port["routerProfile"]["routeList"][0]["portList"] = ["10-70000"]
        with self.assertRaisesRegex(ValueError, "ports"):
            profile_model.parse_acceleration_profile(bad_port)

    def test_observed_default_and_port_range_semantics(self) -> None:
        document = valid_profile()
        signal = document["outboundProfile"]["signalConfig"]
        signal["heartbeatInterval"] = 0
        signal["heartbeatMaxInterval"] = 0
        signal["maxReGetStCount"] = -1
        document["routerProfile"]["routeList"][0]["portList"] = [
            "443~80",
            "0",
        ]

        profile = profile_model.parse_acceleration_profile(document)
        self.assertEqual(profile.signal.heartbeat_interval, 30)
        self.assertEqual(profile.signal.heartbeat_max_interval, 120)
        self.assertEqual(profile.signal.max_ticket_refreshes, 2)
        self.assertEqual(profile.routes[0].ports, ("80-443", "0"))

    def test_input_is_not_mutated(self) -> None:
        document = valid_profile()
        original = copy.deepcopy(document)
        profile_model.parse_acceleration_profile(document)
        self.assertEqual(document, original)


if __name__ == "__main__":
    unittest.main()
