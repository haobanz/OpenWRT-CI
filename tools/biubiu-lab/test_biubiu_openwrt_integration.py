#!/usr/bin/env python3

from pathlib import Path
import json
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "vendor/game-accelerators/biubiu-acc"
LUCI = ROOT / "vendor/game-accelerators/luci-app-biubiu-acc"
CATALOG = CORE / "files/game-catalog.json"


class OpenWrtIntegrationTests(unittest.TestCase):
    def test_firmware_selects_core_and_luci_packages(self) -> None:
        config = (ROOT / "Config/NN6000-DAEDE.txt").read_text()
        self.assertIn("CONFIG_PACKAGE_biubiu-acc=y", config)
        self.assertIn("CONFIG_PACKAGE_luci-app-biubiu-acc=y", config)

        injection = (ROOT / "Scripts/Packages-NN6000-DAEDE.sh").read_text()
        self.assertIn("luci-app-biubiu-acc", injection)

    def test_core_package_installs_management_boundary(self) -> None:
        makefile = (CORE / "Makefile").read_text()
        for path in (
            "biubiu-acc.config",
            "biubiu-acc.init",
            "biubiu-acc-manager",
            "biubiu-acc-supervisor",
            "biubiu-acc-traffic",
            "biubiu-accd.c",
            "game-catalog.json",
        ):
            self.assertIn(path, makefile)
        self.assertIn("/etc/config/biubiu-acc", makefile)

    def test_sms_code_is_not_forwarded_in_process_arguments(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        self.assertIn('printf \'%s\\n\' "$code" | "$CLI" sms-login-stdin', manager)
        self.assertNotIn('sms-login "$phone" "$code"', manager)
        self.assertIn("请求文件权限必须为 0600", manager)

    def test_ui_acl_is_narrow(self) -> None:
        acl = (LUCI / "root/usr/share/rpcd/acl.d/luci-app-biubiu-acc.json").read_text()
        self.assertIn('"/usr/libexec/biubiu-acc-manager request"', acl)
        self.assertIn('"/usr/libexec/biubiu-acc-manager catalog"', acl)
        self.assertIn('"/usr/libexec/biubiu-acc-manager match-status"', acl)
        self.assertNotIn('"/usr/bin/biubiu-accctl *"', acl)
        self.assertNotIn('"/bin/sh *"', acl)

    def test_transport_is_gated_by_authorized_runtime(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        supervisor = (CORE / "files/biubiu-acc-supervisor").read_text()
        traffic = (CORE / "files/biubiu-acc-traffic").read_text()
        daemon = (CORE / "src/biubiu-accd.c").read_text()
        ui = (LUCI / "htdocs/luci-static/resources/view/biubiu-acc/main.js").read_text()
        self.assertIn("CONNTRACK_FILE", manager)
        self.assertIn("match-status) match_status", manager)
        self.assertIn('printf "FLOW\\t%s', manager)
        self.assertNotIn('printf "FLOW\\\\t%s', manager)
        self.assertIn("getMatchStatus", ui)
        self.assertIn("实时匹配", ui)
        self.assertIn("activeTab === 'acceleration'", ui)
        self.assertIn("private_json_ready", manager)
        self.assertIn("acceleration_action", manager)
        self.assertIn("profile_fetch", manager)
        self.assertIn("signal_login", manager)
        self.assertIn("channel_renew", manager)
        self.assertIn("ready", manager)
        self.assertIn("accelerating", manager)
        self.assertNotIn("transport_incomplete", supervisor)
        self.assertNotIn("数据通道尚未实现", ui)
        self.assertIn("biubiu-accd", traffic)
        self.assertIn("nft", traffic)
        self.assertIn("BOLT_COMMAND_ASSOCIATE_REQUEST", daemon)
        self.assertIn("--self-test", daemon)

    def test_core_self_test_includes_bolt_v3(self) -> None:
        source = (CORE / "src/biubiu-accctl.c").read_text()
        self.assertIn('#define BIUBIU_ACC_VERSION "0.8.0"', source)
        self.assertIn("run_bolt_v3_self_test", source)
        self.assertIn('\\"bolt-v3-frame\\"', source)

    def test_catalog_and_low_cost_scope_are_packaged(self) -> None:
        config = (CORE / "files/biubiu-acc.config").read_text()
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        ui = (LUCI / "htdocs/luci-static/resources/view/biubiu-acc/main.js").read_text()
        self.assertIn("option schema_version '2'", config)
        self.assertIn("option scope 'lan'", config)
        self.assertIn("list selected_game 'steam'", config)
        self.assertIn("list selected_game 'counter-strike-2'", config)
        self.assertIn("list selected_game 'epic-games'", config)
        self.assertIn("catalog_has_game", manager)
        self.assertIn('case "$scope" in lan|device)', manager)
        self.assertIn("biubiu-acc.main.schema_version", (CORE / "Makefile").read_text())
        self.assertIn("selected_games", ui)
        self.assertIn("整个局域网", ui)
        self.assertIn("指定设备", ui)

    def test_builtin_catalog_is_bounded_and_source_attributed(self) -> None:
        document = json.loads(CATALOG.read_text())
        profiles = document["profiles"]
        profile_ids = [profile["id"] for profile in profiles]
        self.assertEqual(len(profile_ids), len(set(profile_ids)))
        self.assertEqual(
            set(profile_ids),
            {"steam", "counter-strike-2", "epic-games"},
        )
        port_pattern = re.compile(r"^[1-9][0-9]{0,4}(?:-[1-9][0-9]{0,4})?$")
        for profile in profiles:
            self.assertTrue(profile["source"].startswith("https://"))
            self.assertIn(
                profile["match_mode"],
                {"provider-profile", "provider-profile-plus-hints"},
            )
            for field in ("tcp_destination_ports", "udp_destination_ports"):
                for port_range in profile[field]:
                    self.assertRegex(port_range, port_pattern)
                    bounds = [int(value) for value in port_range.split("-")]
                    self.assertLessEqual(bounds[0], bounds[-1])
                    self.assertGreaterEqual(bounds[0], 1)
                    self.assertLessEqual(bounds[-1], 65535)
                    self.assertNotIn(port_range, {"80", "443"})

        epic = next(profile for profile in profiles if profile["id"] == "epic-games")
        self.assertEqual(epic["tcp_destination_ports"], [])
        self.assertEqual(epic["udp_destination_ports"], [])
        steam = next(profile for profile in profiles if profile["id"] == "steam")
        self.assertIn("steamcontent.com", steam["excluded_dns_suffixes"])
        self.assertNotIn("steamcontent.com", steam["identity_dns_suffixes"])


if __name__ == "__main__":
    unittest.main()
