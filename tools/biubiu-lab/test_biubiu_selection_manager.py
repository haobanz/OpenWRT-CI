#!/usr/bin/env python3
"""Exercise the production shell boundary with synthetic UCI/provider state."""

from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MANAGER = (ROOT / "vendor/game-accelerators/biubiu-acc/files/biubiu-acc-manager").read_text()


def function(name: str) -> str:
    match = re.search(r"^" + re.escape(name) + r"\(\) \{\n.*?^\}", MANAGER, re.M | re.S)
    if match is None:
        raise AssertionError(f"Missing production function: {name}")
    return match.group()


def value_function(name: str, values: dict, argument: int = 1) -> str:
    cases = []
    for key, value in values.items():
        if isinstance(value, list):
            value = "\n".join(value)
        cases.append(f"{shlex.quote(key)}) printf '%s\\n' {shlex.quote(value)} ;;")
    return f'{name}() {{ case "${argument}" in\n' + "\n".join(cases) + "\nesac; }"


class SelectionManagerTests(unittest.TestCase):
    def run_manager(self, action: str, request=None, current=None, fail_cli=""):
        request = request or {}
        current = current or {}
        names = ["safe_text", "decimal_value", "valid_mac", "save_config",
                 "start_control_context", "control_game_search", "control_game_list",
                 "control_game_options", "load_control_ids"]
        with tempfile.TemporaryDirectory(prefix="biubiu-manager-test-") as directory:
            prefix = str(Path(directory) / "job")
            events = Path(directory) / "events"
            events.touch()
            (Path(directory) / "job.out").write_text(json.dumps({
                "success": True, "game_name": "Provider game", "area_name": "Provider area"
            }))
            script = "\n".join(function(name) for name in names)
            script += "\n" + value_function("request_value", request)
            script += "\n" + value_function("request_values", request)
            script += "\n" + value_function("uci_get", current, 2)
            script += r'''
fail() { printf '%s\n' "$*" >&2; exit 74; }
config_load() { :; }
config_list_foreach() { :; }
catalog_has_game() { return 0; }
valid_game_slug() { return 0; }
valid_ipv4() { return 0; }
uci() { printf 'uci %s\n' "$*" >> "$EVENTS"; }
invalidate_data_plane() { printf 'invalidate\n' >> "$EVENTS"; }
log_event() { :; }
emit_message() { printf 'done\n'; }
jsonfilter() {
    case "$*" in
        *game_name*) printf 'Provider game\n' ;;
        *area_name*) printf 'Provider area\n' ;;
    esac
}
run_cli() {
    printf 'cli %s\n' "$*" >> "$EVENTS"
    [ "$1" != "$FAIL_CLI" ]
}
'''
            script += "\n" + action + "\n"
            result = subprocess.run(["sh", "-c", script], text=True, capture_output=True,
                                    env={**os.environ, "CONFIG": "biubiu-acc", "INIT": "true",
                                         "TEMP_PREFIX": prefix, "EVENTS": str(events),
                                         "FAIL_CLI": fail_cli})
            return result, events.read_text().splitlines()

    @staticmethod
    def config():
        return {"scope": "lan", "target_id": "38780", "area_id": "103",
                "platform_id": "6", "acc_mode": "5", "log_level": "info",
                "openclash_mode": "exclusive", "selected_games": []}

    def test_search_uses_native_catalog_and_public_view(self):
        result, events = self.run_manager("control_game_search", {"keyword": "Counter"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events, ["cli pc-game-search pc-game-search Counter 1 12",
                                  "cli pc-game-catalog pc-game-catalog"])
        self.assertTrue(json.loads(result.stdout)["success"])

    def test_options_fetches_requested_game_then_public_view(self):
        result, events = self.run_manager("control_game_options", {"game_id": "38780"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events, ["cli pc-game-map pc-game-map 38780",
                                  "cli pc-game-options pc-game-options 38780"])

    def test_search_failure_does_not_return_previous_cache(self):
        result, events = self.run_manager("control_game_search", {"keyword": "Counter"},
                                         fail_cli="pc-game-search")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertEqual(len(events), 1)

    def test_selection_is_validated_before_invalidation_and_save(self):
        request = {**self.config(), "game_name": "Spoof", "area_name": "Spoof"}
        result, events = self.run_manager("save_config", request)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events[:3], ["cli pc-game-map pc-game-map 38780",
                                      "cli pc-game-selection pc-game-selection 38780 103 6 5",
                                      "invalidate"])
        self.assertIn("uci -q set biubiu-acc.main.game_name=Provider game", events)
        self.assertIn("uci -q set biubiu-acc.main.area_name=Provider area", events)
        self.assertNotIn("Spoof", "\n".join(events))

    def test_invalid_provider_selection_leaves_active_config_intact(self):
        result, events = self.run_manager("save_config", self.config(),
                                         fail_cli="pc-game-selection")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(e == "invalidate" or e.startswith("uci ") for e in events))

    def test_mode_change_invalidates_even_with_same_game_and_area(self):
        request = self.config()
        current = {**request, "acc_mode": "3"}
        result, events = self.run_manager("save_config", request, current)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("invalidate", events)
        self.assertIn("cli pc-game-selection pc-game-selection 38780 103 6 5", events)

    def test_partial_selection_is_rejected(self):
        request = {**self.config(), "area_id": ""}
        result, events = self.run_manager("save_config", request)
        self.assertEqual(result.returncode, 74)
        self.assertEqual(events, [])

    def test_unchanged_selection_preserves_provider_labels_offline(self):
        request = self.config()
        current = {**request, "game_name": "Existing game", "area_name": "Existing area"}
        result, events = self.run_manager("save_config", request, current)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(any(e.startswith("cli ") for e in events))
        self.assertIn("uci -q set biubiu-acc.main.game_name=Existing game", events)

    def test_context_receives_selected_mode_or_provider_default(self):
        setup = 'CONTROL_GAME_ID=38780; CONTROL_AREA_ID=103; CONTROL_PLATFORM_ID=6; '
        result, events = self.run_manager(setup + 'CONTROL_ACC_MODE=5; start_control_context')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events, ["cli pc-context-start pc-context-start 38780 103 6 5"])
        result, events = self.run_manager(setup + 'CONTROL_ACC_MODE=; start_control_context')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events, ["cli pc-context-start pc-context-start 38780 103 6"])

    def test_reselecting_legacy_ids_fills_provider_labels(self):
        current = self.config()
        request = {**current, "game_name": "From picker"}
        result, events = self.run_manager("save_config", request, current)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cli pc-game-selection pc-game-selection 38780 103 6 5", events)
        self.assertIn("uci -q set biubiu-acc.main.game_name=Provider game", events)


if __name__ == "__main__":
    unittest.main()
