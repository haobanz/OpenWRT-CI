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
            "bbnet_bridge.cpp",
            "bbnet_transport.c",
            "confluence_codec.c",
            "quicknet",
            "game-catalog.json",
        ):
            self.assertIn(path, makefile)
        self.assertIn("/etc/config/biubiu-acc", makefile)
        self.assertIn("+coreutils-stat", makefile)

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

    def test_official_picker_requires_matching_control_package(self) -> None:
        makefile = (LUCI / "Makefile").read_text()
        self.assertIn("EXTRA_DEPENDS:=biubiu-acc (>=0.11.0-r2)", makefile)

    def test_transport_is_gated_by_authorized_runtime(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        supervisor = (CORE / "files/biubiu-acc-supervisor").read_text()
        traffic = (CORE / "files/biubiu-acc-traffic").read_text()
        init = (CORE / "files/biubiu-acc.init").read_text()
        daemon = (CORE / "src/biubiu-accd.c").read_text()
        control = (CORE / "src/biubiu-accctl.c").read_text()
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
        self.assertIn("fetch_key()", manager)
        self.assertIn("fetch_key) fetch_key", manager)
        self.assertIn("ready", manager)
        self.assertIn("accelerating", manager)
        self.assertNotIn("transport_incomplete", supervisor)
        self.assertNotIn("数据通道尚未实现", ui)
        self.assertIn("biubiu-accd", traffic)
        self.assertIn("nft", traffic)
        self.assertIn("json_add_string message", traffic)
        self.assertNotIn('"$TRAFFIC" start', init)
        self.assertIn("getsockname(fd", daemon)
        self.assertIn("bip_address", daemon)
        self.assertNotIn("tertiary_address", daemon)
        self.assertIn("BBNET_APPLICATION_KCP", daemon)
        self.assertIn("BBNET_APPLICATION_NACK", daemon)
        self.assertIn("open_bolt_tcp_link", daemon)
        self.assertIn("biubiu_udp_tunnel_encode", daemon)
        self.assertIn("route_context != channel->session_id", daemon)
        self.assertIn("--self-test", daemon)
        self.assertIn("renew_data_plane", supervisor)
        self.assertIn("channel_expiry", supervisor)
        self.assertIn("stop_unusable_data_plane", supervisor)
        self.assertIn("runtime_structure_ready", supervisor)
        self.assertIn("runtime_usable", supervisor)
        self.assertIn("runtime authorization was removed", supervisor)
        self.assertIn("runtime authorization has expired", supervisor)
        self.assertIn("Rules[*]", traffic)
        self.assertIn("provider CIDR", traffic)
        self.assertIn("profile_domains", traffic)
        self.assertIn("valid_domain", traffic)
        self.assertIn("resolve_domain", traffic)
        self.assertIn("refresh_domain_sets", traffic)
        self.assertIn("reload", traffic)
        self.assertIn('TRAFFIC" reload', supervisor)
        self.assertIn('TRAFFIC" refresh', supervisor)
        self.assertIn("route_matches", manager)
        self.assertIn("provider_rules_available", manager)
        self.assertIn("provider_domain_rules_available", manager)
        self.assertIn("provider_domain_rules_from_nft", manager)
        self.assertIn("tcpDomains", manager)
        self.assertIn("udpDomains", manager)
        self.assertIn("tcpDomains", control)
        self.assertIn("udpDomains", control)
        self.assertIn("hasRouteHint", ui)
        self.assertIn("hasProviderDomainHint", ui)
        self.assertIn("start_data_plane", supervisor)
        self.assertIn("START_COOLDOWN", supervisor)
        self.assertIn("tcp_worker_count", daemon)
        self.assertIn("wait_for_tcp_workers", daemon)
        self.assertIn("native_seed_public_key_der_b64", control)
        self.assertIn("load_native_seed_acceleration_key", control)
        self.assertIn("load_acceleration_key_or_seed", control)
        self.assertIn("parse_native_key_rotation", control)
        self.assertIn('string_member(response, "rsaPublicKey")', control)
        self.assertNotIn("BOOTSTRAP_CONFIG_ENDPOINT", control)
        self.assertNotIn("bootstrap_config_request", control)
        self.assertNotIn("bootstrap-config-fetch", control)
        self.assertNotIn("config.getSecurityKey", control)
        self.assertIn("fetch_acceleration_key", control)
        self.assertIn("acc-key-fetch", control)
        self.assertIn('#define MAGA_APP_ID "25344054"', control)
        self.assertIn('#define MAGA_PACKAGE_NAME "com.njh.biubiu"', control)
        self.assertIn('Content-Type: application/json', control)
        self.assertIn('x-biu-client:', control)
        self.assertIn('MAGA_USER_AGENT', control)
        self.assertNotIn('x-mg-agent: 2', control)
        self.assertIn('session_cookie_header', control)
        self.assertIn('json_object_object_add(extensions, "st"', control)
        self.assertIn('json_object_object_add(client, "biuid"', control)
        self.assertNotIn('json_object_object_add(extensions, "biuid"', control)
        self.assertIn('pc_biubiu_id, service_ticket, client_kind);', control)
        self.assertIn('MAX_COOKIE_HEADER_SIZE 8192U', control)
        self.assertIn('strcmp(host, ACCELERATION_HOST) != 0', control)
        self.assertIn('OPENSSL_cleanse(cookie_header', control)
        self.assertIn('cleanse_cookie_headers(headers)', control)
        self.assertIn('client_kind == ACCELERATION_CLIENT_MOBILE &&', control)
        self.assertIn('"%s%s&df=adat"', control)
        self.assertIn("acceleration_api_request_once", control)
        self.assertIn("store_acceleration_key(acceleration_key_file, &key)", control)
        self.assertIn('value == 200 || value == 2000000 || value == 2000001', control)
        self.assertIn('acceleration_response_state_code', control)
        self.assertIn('acceleration_response_state_message', control)
        self.assertNotIn('json_object_add(data, "lastSortKey", json_object_new_string(""))', control)
        self.assertIn("PC_GAME_LIST_ENDPOINT", control)
        self.assertIn('"/api/ping-server.game.pc.gameList?ver=1.0.1"', control)
        self.assertIn('"pc-game-list"', control)
        self.assertIn("PC_GAME_SEARCH_ENDPOINT", control)
        self.assertIn('"/api/ping-feed.search.game.pc?ver=1.0.1"', control)
        self.assertIn('"limitPlatformIds"', control)
        self.assertIn('json_object_new_int(6)', control)
        self.assertIn('"pc-game-search"', control)
        self.assertIn("PC_GAME_PROFILE_ENDPOINT", control)
        self.assertIn('"/api/ping-server.game.pc.getGameProfile?ver=1.0.0"', control)
        self.assertIn('"pc-game-profile"', control)
        self.assertIn("DEFAULT_PC_GAME_PROFILE_FILE", control)
        self.assertIn("PC_GAME_MAP_ENDPOINT", control)
        self.assertIn('"/api/ping-server.game.pc.map?ver=1.0.0"', control)
        self.assertIn('"gameIds"', control)
        self.assertIn('"pc-game-map"', control)
        self.assertIn("DEFAULT_PC_GAME_MAP_FILE", control)
        self.assertIn("DEFAULT_PC_GAME_LIST_FILE", control)
        self.assertIn("DEFAULT_PC_ENTITLEMENT_FILE", control)
        self.assertIn("DEFAULT_PC_USER_FILE", control)
        self.assertIn("PC_USER_INFO_ENDPOINT", control)
        self.assertIn('"pc-user-sync"', control)
        self.assertIn('"pc-check-speedup"', control)
        self.assertIn('PC_CHECK_SPEEDUP_ENDPOINT', control)
        self.assertIn('"/api/ping-server.biuvpn.game.checkSpeedup?ver=1.0.0"', control)
        self.assertIn('"lastJitterTime"', control)
        self.assertIn('"lighthouseList"', control)
        self.assertIn('"transferLighthouseList"', control)
        self.assertIn('"pt90Ms"', control)
        self.assertIn('"x-biu-client: %s"', control)
        self.assertIn("if (client_header)", control)
        self.assertIn('"x-biu-ver: %.*s"', control)
        self.assertIn(
            'asprintf(&version_header, "x-biu-ver: %s", PC_APP_VERSION)',
            control,
        )
        self.assertNotIn(
            'asprintf(&version_header, "x-biu-ver: %s",\n'
            '                     SIGNAL_ENGINE_VERSION)',
            control,
        )
        self.assertIn(
            '"/api/ping-signal.open.login.loginV2?ver=1.0.0&df=adat"',
            control,
        )
        self.assertIn(
            '"/api/ping-signal.open.auth.getChannelStV2?ver=1.0.0&df=adat"',
            control,
        )
        self.assertNotIn('"x-mg-appkey: %s"', control)
        self.assertIn('"platform: windows"', control)
        client_build = control.index('client = build_acceleration_client(',
                                     control.index('static int acceleration_api_request_once'))
        signal_body = control.index('if (native_signal_login || native_channel_ticket)', client_build)
        self.assertLess(client_build, signal_body)
        self.assertIn("build_native_control_request", control)
        self.assertNotIn("build_native_signal_login_request", control)
        self.assertIn('json_object_new_string(""));', control)
        self.assertNotIn("load_pc_signal_context(&acc_pod_id, NULL)", control)
        self.assertNotIn("build_signal_login_envelope", control)
        self.assertIn('json_object_object_add(data, "list", channel_list)', control)
        self.assertNotIn("native_plain_response", control)
        self.assertIn("decrypt_signal_response(outer, result)", control)
        self.assertIn('#define PC_APP_ID "biubiu"', control)
        self.assertIn('#define PC_APP_VERSION "1.0.0.0"', control)
        self.assertIn('#define PC_APP_VERSION_CODE "1000000"', control)
        self.assertIn('#define PC_BUILD_ID ""', control)
        self.assertIn('"/api/ping-server.config.base.list?ver=1.0.0"', control)
        self.assertIn('ACCELERATION_CLIENT_WINDOWS', control)
        self.assertIn('json_object_new_string("windows")', control)
        pc_check = control[control.index("static int run_pc_check_speedup"):control.index("static int run_profile_request")]
        self.assertIn('"useMemberSpeedUpExperience"', pc_check)
        self.assertNotIn('"space"', pc_check)
        self.assertIn('control-response-codes', control)
        self.assertIn('"cookieTransportReady"', control)
        self.assertIn('\\"session-cookie-transport\\"', control)
        self.assertIn('schema = json_object_get_int64(schema_version)', daemon)
        self.assertIn('schema != 2', daemon)
        self.assertIn("BOLT_BIND_REQUEST_TYPE", daemon)
        self.assertIn("BOLT_BIND_RESPONSE_TYPE", daemon)
        self.assertIn("channel_token", daemon)
        self.assertIn("bind_runtime_channels", daemon)
        self.assertIn('signalSessionId', daemon)
        self.assertIn("自动获取", ui)
        self.assertIn("operation: 'fetch_key'", ui)
        self.assertIn("session.cookie_transport_ready", ui)
        self.assertIn("static bool adat_outer_code", control)
        self.assertIn("unable to decrypt acceleration response (ADAT c=%d)", control)

    def test_renewed_channel_ticket_rebuilds_runtime(self) -> None:
        source = (CORE / "src/biubiu-accctl.c").read_text()
        self.assertIn("DEFAULT_CHANNEL_TICKET_FILE", source)
        self.assertIn("renewed_channels", source)
        self.assertIn('"expiresAt"', source)
        self.assertIn('"bip"', source)
        self.assertNotIn('json_object_object_add(wire, "cip"', source)
        self.assertIn('"dataChannelSessionId"', source)
        self.assertIn('"secretType"', source)
        renew = source[source.index("static int run_channel_renew"):source.index("static int run_runtime_prepare")]
        self.assertIn('json_object_new_int(0)', renew)
        runtime_prepare = source[source.index("static int run_runtime_prepare"):source.index("static void print_login_summary")]
        self.assertNotIn('"bport"', runtime_prepare)
        self.assertNotIn('"cport"', runtime_prepare)

    def test_profile_uses_a_nonempty_client_session_and_dataplane_harness(self) -> None:
        control = (CORE / "src/biubiu-accctl.c").read_text()
        harness = (ROOT / "tools/biubiu-lab/biubiu_accd_dataplane_test.c").read_text()
        self.assertIn("static char *new_client_session_id", control)
        self.assertIn('json_object_new_string(client_session_id)', control)
        self.assertIn("tcp-confluence-raw-stream", harness)
        self.assertIn("udp-bbnet-reverse-tuple", harness)

    def test_config_change_invalidates_before_reload(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        save = manager[manager.index("save_config()"):manager.index("send_sms()")]
        self.assertLess(save.index("invalidate_data_plane"), save.index("$INIT reload"))

    def test_profile_control_prepares_the_full_data_plane(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        flow = manager[manager.index("control_profile_fetch()"):manager.index("control_signal_login()")]
        self.assertLess(flow.index("profile-fetch"), flow.index("signal-login"))
        self.assertLess(flow.index("signal-login"), flow.index("runtime-prepare"))
        self.assertIn("invalidate_data_plane", flow)
        ui = (LUCI / "htdocs/luci-static/resources/view/biubiu-acc/main.js").read_text()
        self.assertIn("获取节点并授权", ui)
        self.assertIn("pc-user-sync", flow)
        self.assertIn("pc-game-map", flow)
        self.assertIn("start_control_context", flow)
        self.assertIn("pc-check-speedup", flow)
        self.assertIn("pc-profile-fetch", flow)
        self.assertIn("pc-signal-login", flow)
        self.assertIn("pc-runtime-prepare", flow)
        self.assertIn("game_options", ui)
        self.assertLess(flow.index("pc-game-map"), flow.index("start_control_context"))

    def test_native_start_context_is_derived_from_provider_metadata(self) -> None:
        control = (CORE / "src/biubiu-accctl.c").read_text()
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        supervisor = (CORE / "files/biubiu-acc-supervisor").read_text()
        workflow = (ROOT / ".github/workflows/DAEDE-Build.yml").read_text()
        upgrade = (CORE / "files/biubiu-acc.upgrade").read_text()
        self.assertIn("pc_game_start_selection", control)
        self.assertIn('"accModeList"', control)
        self.assertIn('"accPodId"', control)
        self.assertIn('json_object_new_string("auto")', control)
        self.assertIn('json_object_object_add(context, "serverId", json_object_new_int(0))', control)
        self.assertIn('"pc", "switch", "ps", "xbox", "steamdeck"', control)
        self.assertNotIn('const char *platform_name = "Steam"', control)
        profile = control[control.index("static int run_pc_profile_request"):control.index("static int run_profile_request")]
        self.assertIn("pc_profile_context_mode", profile)
        self.assertIn('json_object_object_add(data, "serverId", json_object_new_int(0))', profile)
        self.assertNotIn("SERVER_ID SPEEDUP_MODEL_ID", control)
        self.assertIn("CONTROL_PLATFORM_ID=6", manager)
        self.assertNotIn("CONTROL_PLATFORM_NAME", manager)
        self.assertIn('"$CONTROL_ACC_MODE"', manager)
        self.assertIn("platform_id=6", supervisor)
        self.assertIn("pc-game-map.json", upgrade)
        self.assertIn("biubiu_accctl_context_test.c", workflow)

    def test_core_self_test_includes_bolt_v3(self) -> None:
        source = (CORE / "src/biubiu-accctl.c").read_text()
        package_version = re.search(r'^PKG_VERSION:=(\S+)$',
                                    (CORE / "Makefile").read_text(), re.MULTILINE)
        source_version = re.search(r'^#define BIUBIU_ACC_VERSION "([^"]+)"$',
                                   source, re.MULTILINE)
        self.assertIsNotNone(package_version)
        self.assertIsNotNone(source_version)
        self.assertEqual(source_version.group(1), package_version.group(1))
        self.assertIn("run_bolt_v3_self_test", source)
        self.assertIn('\\"bolt-v3-frame\\"', source)
        daemon = (CORE / "src/biubiu-accd.c").read_text()
        self.assertIn("bolt-v2-bind", daemon)

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
        cs2 = next(
            profile for profile in profiles if profile["id"] == "counter-strike-2"
        )
        self.assertEqual(
            cs2["provider_profile"],
            {
                "game_id": 38780,
                "area_id": 146,
                "platform_id": 6,
                "platform_name": "pc",
                "acceleration_modes": [3, 5],
            },
        )


if __name__ == "__main__":
    unittest.main()
