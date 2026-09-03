#!/usr/bin/env python3

import json
from pathlib import Path
import unittest

import biubiu_match_model as match_model


CATALOG_PATH = (
    Path(__file__).resolve().parents[2]
    / "vendor/game-accelerators/biubiu-acc/files/game-catalog.json"
)


class MatchModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = json.loads(CATALOG_PATH.read_text())
        cls.catalog = match_model.load_catalog(cls.document)

    def test_whole_lan_plan_merges_overlapping_steam_ranges(self) -> None:
        plan = match_model.compile_match_plan(
            self.catalog,
            ["steam", "counter-strike-2", "steam"],
            scope="lan",
        )
        self.assertIsNone(plan.source_cidr)
        self.assertEqual(plan.selected_games, ("steam", "counter-strike-2"))
        self.assertEqual(
            [value.render() for value in plan.tcp_destination_ports],
            ["27015-27050"],
        )
        self.assertEqual(
            [value.render() for value in plan.udp_destination_ports],
            ["3478", "4379-4380", "27000-27250"],
        )
        self.assertNotIn("80", plan.as_mapping()["tcp_destination_ports"])
        self.assertNotIn("443", plan.as_mapping()["tcp_destination_ports"])
        self.assertFalse(plan.as_mapping()["applied"])

    def test_device_plan_requires_and_normalizes_ipv4(self) -> None:
        plan = match_model.compile_match_plan(
            self.catalog,
            ["counter-strike-2"],
            scope="device",
            target_ip="192.168.100.175",
        )
        self.assertEqual(plan.source_cidr, "192.168.100.175/32")
        with self.assertRaisesRegex(ValueError, "valid IPv4"):
            match_model.compile_match_plan(
                self.catalog,
                ["counter-strike-2"],
                scope="device",
                target_ip="2001:db8::1",
            )

    def test_epic_uses_provider_profile_without_broad_port_hints(self) -> None:
        plan = match_model.compile_match_plan(
            self.catalog,
            ["epic-games"],
            scope="lan",
        )
        self.assertEqual(plan.provider_profiles, ("epic-games",))
        self.assertEqual(plan.tcp_destination_ports, ())
        self.assertEqual(plan.udp_destination_ports, ())

    def test_content_domains_are_explicitly_excluded(self) -> None:
        plan = match_model.compile_match_plan(
            self.catalog,
            ["steam"],
            scope="lan",
        )
        self.assertIn("steamcontent.com", plan.excluded_dns_suffixes)
        self.assertNotIn("steamcontent.com", plan.identity_dns_suffixes)
        self.assertIn("steampowered.com", plan.identity_dns_suffixes)

    def test_invalid_selection_and_catalog_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least one game"):
            match_model.compile_match_plan(self.catalog, [], scope="lan")
        with self.assertRaisesRegex(ValueError, "not in the catalog"):
            match_model.compile_match_plan(self.catalog, ["unknown"], scope="lan")

        duplicate = json.loads(json.dumps(self.document))
        duplicate["profiles"].append(duplicate["profiles"][0])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            match_model.load_catalog(duplicate)


if __name__ == "__main__":
    unittest.main()
