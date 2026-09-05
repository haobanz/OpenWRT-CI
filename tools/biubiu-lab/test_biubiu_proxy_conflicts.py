#!/usr/bin/env python3
"""Exercise biubiu-acc traffic proxy ownership with synthetic shell services."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
TRAFFIC = ROOT / "vendor/game-accelerators/biubiu-acc/files/biubiu-acc-traffic"


def production_functions() -> str:
    source = TRAFFIC.read_text()
    source = source.split('case "${1:-}" in', 1)[0]
    return source.replace(". /lib/functions.sh\n", "").replace(
        ". /usr/share/libubox/jshn.sh\n", ""
    )


HARNESS = r'''#!/bin/sh
set -u

FUNCTIONS=$1
TEST_ROOT=$2
SCENARIO=$3
. "$FUNCTIONS"

RUNTIME_ROOT="$TEST_ROOT/runtime"
STATE_FILE="$RUNTIME_ROOT/traffic.json"
PROXY_STATE_FILE="$RUNTIME_ROOT/proxies.json"
PID_FILE="$RUNTIME_ROOT/engine.pid"
RUNTIME_FILE="$TEST_ROOT/runtime.json"
LOG_FILE="$TEST_ROOT/engine.log"
EVENT_LOG="$TEST_ROOT/events.log"
NFT_CAPTURE="$TEST_ROOT/nft.rules"
ENGINE=/bin/false
mkdir -p "$RUNTIME_ROOT"
: > "$RUNTIME_FILE"
: > "$EVENT_LOG"

json_init() { JSON_PAIRS=''; }
json_add_raw() {
    if [ -n "$JSON_PAIRS" ]; then JSON_PAIRS="$JSON_PAIRS,"; fi
    JSON_PAIRS="$JSON_PAIRS\"$1\":$2"
}
json_add_boolean() { json_add_raw "$1" "$2"; }
json_add_int() { json_add_raw "$1" "$2"; }
json_add_string() { json_add_raw "$1" "\"$2\""; }
json_dump() { printf '{%s}\n' "$JSON_PAIRS"; }

jsonfilter() {
    local file='' expression='' key=''
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -i) file=$2; shift 2 ;;
            -e) expression=$2; shift 2 ;;
            *) shift ;;
        esac
    done
    if [ "$file" = "$RUNTIME_FILE" ]; then
        return 1
    fi
    case "$expression" in
        '@.openclash_was_running') key='openclash_was_running' ;;
        '@.openclash_stop_intent') key='openclash_stop_intent' ;;
        '@.openclash_stopped') key='openclash_stopped' ;;
        '@.daed_was_running') key='daed_was_running' ;;
        '@.daed_stop_intent') key='daed_stop_intent' ;;
        '@.daed_stopped') key='daed_stopped' ;;
        '@.dae_was_running') key='dae_was_running' ;;
        '@.dae_stop_intent') key='dae_stop_intent' ;;
        '@.dae_stopped') key='dae_stopped' ;;
        *) return 1 ;;
    esac
    [ -f "$file" ] || return 1
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\\(true\\|false\\|[0-9][0-9]*\\).*/\\1/p" "$file" | head -n 1
}

logger() { printf 'log:%s\n' "$*" >> "$EVENT_LOG"; }
id() { printf '0\n'; }
sleep() { :; }
kill() { return 1; }
ip() { return 0; }
nft() {
    if [ "${1:-}" = '-f' ]; then
        cat > "$NFT_CAPTURE"
    fi
    return 0
}
config_value() {
    case "$1" in
        scope) printf 'lan\n' ;;
        target_ip) printf '\n' ;;
        openclash_mode) printf 'exclusive\n' ;;
        *) printf '%s\n' "${2:-}" ;;
    esac
}
port_hints() { tcp_ports='27015'; udp_ports=''; return 0; }

RUN_OPENCLASH=0
RUN_DAED=0
RUN_DAE=0
STOP_FAIL=''
STOP_FAIL_STOPS=0
START_FAIL=''
STOP_DELAY_SERVICE=''
STOP_DELAY_POLLS=0
START_DELAY_SERVICE=''
START_DELAY_POLLS=0
PENDING_STOP_SERVICE=''
PENDING_STOP_POLLS=0
PENDING_START_SERVICE=''
PENDING_START_POLLS=0
WAIT_TICKS=0

service_is_running() {
    case "$1" in
        openclash) [ "$RUN_OPENCLASH" -eq 1 ] ;;
        daed) [ "$RUN_DAED" -eq 1 ] ;;
        dae) [ "$RUN_DAE" -eq 1 ] ;;
        *) return 1 ;;
    esac
}
set_service_running() {
    case "$1" in
        openclash) RUN_OPENCLASH=$2 ;;
        daed) RUN_DAED=$2 ;;
        dae) RUN_DAE=$2 ;;
        *) return 1 ;;
    esac
}
advance_service_delays() {
    if [ -n "$PENDING_STOP_SERVICE" ]; then
        if [ "$PENDING_STOP_POLLS" -gt 0 ]; then
            PENDING_STOP_POLLS=$((PENDING_STOP_POLLS - 1))
        else
            set_service_running "$PENDING_STOP_SERVICE" 0
            PENDING_STOP_SERVICE=''
        fi
    fi
    if [ -n "$PENDING_START_SERVICE" ]; then
        if [ "$PENDING_START_POLLS" -gt 0 ]; then
            PENDING_START_POLLS=$((PENDING_START_POLLS - 1))
        else
            set_service_running "$PENDING_START_SERVICE" 1
            PENDING_START_SERVICE=''
        fi
    fi
}
proxy_service_running() {
    advance_service_delays
    service_is_running "$1"
}
proxy_wait_tick() { WAIT_TICKS=$((WAIT_TICKS + 1)); }
proxy_service_stop() {
    printf 'stop:%s\n' "$1" >> "$EVENT_LOG"
    if [ "$STOP_FAIL" = "$1" ]; then
        [ "$STOP_FAIL_STOPS" -eq 1 ] && set_service_running "$1" 0
        return 1
    fi
    if [ "$STOP_DELAY_SERVICE" = "$1" ]; then
        PENDING_STOP_SERVICE=$1
        PENDING_STOP_POLLS=$STOP_DELAY_POLLS
        return 0
    fi
    set_service_running "$1" 0
}
proxy_service_start() {
    printf 'start:%s\n' "$1" >> "$EVENT_LOG"
    [ "$START_FAIL" = "$1" ] && return 1
    if [ "$START_DELAY_SERVICE" = "$1" ]; then
        PENDING_START_SERVICE=$1
        PENDING_START_POLLS=$START_DELAY_POLLS
        return 0
    fi
    set_service_running "$1" 1
}

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
expect() { [ "$1" = "$2" ] || fail "expected $1, got $2"; }
expect_pending() { proxy_state_service_stopped "$1" || fail "$1 should remain pending"; }
expect_not_pending() {
    if proxy_state_service_stopped "$1"; then fail "$1 should not be pending"; fi
    return 0
}
event_count() { grep -c "^$1\\($\\|:\\)" "$EVENT_LOG" 2>/dev/null || true; }
seed_stopped() {
    PROXY_OC_WAS=0; PROXY_OC_INTENT=0; PROXY_OC_STOPPED=0
    PROXY_DAED_WAS=0; PROXY_DAED_INTENT=0; PROXY_DAED_STOPPED=0
    PROXY_DAE_WAS=0; PROXY_DAE_INTENT=0; PROXY_DAE_STOPPED=0
    case "$1" in
        openclash) PROXY_OC_WAS=1; PROXY_OC_STOPPED=1 ;;
        daed) PROXY_DAED_WAS=1; PROXY_DAED_STOPPED=1 ;;
        dae) PROXY_DAE_WAS=1; PROXY_DAE_STOPPED=1 ;;
        *) fail "bad seed $1" ;;
    esac
    proxy_state_write || fail 'cannot seed proxy state'
}

case "$SCENARIO" in
    only-openclash)
        RUN_OPENCLASH=1
        stop_conflicting_proxies exclusive || fail 'OpenClash conflict stop failed'
        expect "$RUN_OPENCLASH" 0
        expect_pending openclash
        expect "$(event_count stop:openclash)" 1
        stop_all || fail 'OpenClash recovery failed'
        expect "$RUN_OPENCLASH" 1
        expect_not_pending openclash
        ;;
    only-daed)
        RUN_DAED=1
        stop_conflicting_proxies exclusive || fail 'daed conflict stop failed'
        expect "$RUN_DAED" 0
        expect_pending daed
        stop_all || fail 'daed recovery failed'
        expect "$RUN_DAED" 1
        expect_not_pending daed
        ;;
    only-dae)
        RUN_DAE=1
        stop_conflicting_proxies exclusive || fail 'dae conflict stop failed'
        expect "$RUN_DAE" 0
        expect_pending dae
        stop_all || fail 'dae recovery failed'
        expect "$RUN_DAE" 1
        expect_not_pending dae
        ;;
    both-stopped)
        stop_conflicting_proxies exclusive || fail 'stopped services should not conflict'
        stop_all || fail 'empty recovery failed'
        expect "$(event_count stop)" 0
        expect "$(event_count start)" 0
        ;;
    partial-stop-failure)
        RUN_OPENCLASH=1
        RUN_DAED=1
        STOP_FAIL=daed
        if stop_conflicting_proxies exclusive; then fail 'partial stop should fail'; fi
        expect "$RUN_OPENCLASH" 1
        expect "$RUN_DAED" 1
        expect "$(event_count start:openclash)" 1
        expect "$(event_count start:daed)" 0
        expect_not_pending openclash
        expect_not_pending daed
        ;;
    start-failure)
        RUN_OPENCLASH=1
        seed_stopped daed
        if start_all; then fail 'mock engine should fail after conflict acquisition'; fi
        expect "$RUN_OPENCLASH" 1
        expect "$RUN_DAED" 0
        expect "$(event_count start:openclash)" 1
        expect "$(event_count start:daed)" 0
        expect_pending daed
        expect_not_pending openclash
        ;;
    reload-keeps-ownership)
        seed_stopped openclash
        if reload_all; then fail 'mock engine should fail during reload'; fi
        expect "$RUN_OPENCLASH" 0
        expect "$(event_count start:openclash)" 0
        expect_pending openclash
        ;;
    repeated-stop)
        RUN_OPENCLASH=1
        stop_conflicting_proxies exclusive || fail 'initial stop failed'
        stop_all || fail 'initial restore failed'
        stop_all || fail 'idempotent restore failed'
        expect "$RUN_OPENCLASH" 1
        expect "$(event_count start:openclash)" 1
        ;;
    restore-retry)
        RUN_OPENCLASH=1
        stop_conflicting_proxies exclusive || fail 'initial stop failed'
        START_FAIL=openclash
        if stop_all; then fail 'restore failure should propagate'; fi
        expect "$RUN_OPENCLASH" 0
        expect_pending openclash
        START_FAIL=''
        stop_all || fail 'retry restore failed'
        expect "$RUN_OPENCLASH" 1
        expect_not_pending openclash
        ;;
    delayed-ready)
        RUN_OPENCLASH=1
        STOP_DELAY_SERVICE=openclash
        STOP_DELAY_POLLS=3
        START_DELAY_SERVICE=openclash
        START_DELAY_POLLS=3
        stop_conflicting_proxies exclusive || fail 'delayed stop was not confirmed'
        expect "$RUN_OPENCLASH" 0
        stop_all || fail 'delayed start was not confirmed'
        expect "$RUN_OPENCLASH" 1
        expect "$(event_count start:openclash)" 1
        [ "$WAIT_TICKS" -ge 6 ] || fail 'bounded wait did not poll delayed service'
        ;;
    legacy-openclash)
        printf '{"openclash_was_running":true}\n' > "$STATE_FILE"
        stop_all || fail 'legacy recovery failed'
        expect "$RUN_OPENCLASH" 1
        [ ! -e "$PROXY_STATE_FILE" ] || fail 'legacy proxy record was not cleared'
        ;;
    *) fail "unknown scenario $SCENARIO" ;;
esac
'''


class ProxyConflictTests(unittest.TestCase):
    def run_scenario(self, scenario: str) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            functions = root / "traffic-functions.sh"
            harness = root / "harness.sh"
            functions.write_text(production_functions())
            harness.write_text(textwrap.dedent(HARNESS))
            functions.chmod(0o700)
            harness.chmod(0o700)
            result = subprocess.run(
                ["/bin/sh", str(harness), str(functions), str(root), scenario],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                result.returncode,
                0,
                f"{scenario} failed\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )

    def test_each_supported_running_proxy_is_stopped_and_restored(self) -> None:
        for scenario in ("only-openclash", "only-daed", "only-dae"):
            with self.subTest(scenario=scenario):
                self.run_scenario(scenario)

    def test_already_stopped_proxies_are_never_started(self) -> None:
        self.run_scenario("both-stopped")

    def test_partial_stop_failure_restores_only_the_service_stopped_this_round(self) -> None:
        self.run_scenario("partial-stop-failure")

    def test_start_failure_preserves_existing_ownership_and_rolls_back_new_stop(self) -> None:
        self.run_scenario("start-failure")

    def test_reload_keeps_proxy_ownership_on_failure(self) -> None:
        self.run_scenario("reload-keeps-ownership")

    def test_stop_is_idempotent_and_restore_failure_is_retryable(self) -> None:
        self.run_scenario("repeated-stop")
        self.run_scenario("restore-retry")

    def test_stop_and_restore_wait_for_delayed_process_state(self) -> None:
        self.run_scenario("delayed-ready")

    def test_legacy_traffic_state_recovers_openclash(self) -> None:
        self.run_scenario("legacy-openclash")


if __name__ == "__main__":
    unittest.main()
