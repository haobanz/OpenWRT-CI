#!/usr/bin/env python3
"""Run production start_all with a synthetic nft command and capture its input."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
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
. "$FUNCTIONS"

RUNTIME_ROOT="$TEST_ROOT/runtime"
RUNTIME_FILE="$TEST_ROOT/runtime.json"
STATE_FILE="$RUNTIME_ROOT/traffic.json"
PROXY_STATE_FILE="$RUNTIME_ROOT/proxies.json"
PID_FILE="$RUNTIME_ROOT/engine.pid"
LOG_FILE="$TEST_ROOT/engine.log"
NFT_CAPTURE="$TEST_ROOT/biubiu-acc.nft"
ENGINE=/bin/false
mkdir -p "$RUNTIME_ROOT"
: > "$RUNTIME_FILE"

jsonfilter() {
    local file='' expression=''
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -i) file=$2; shift 2 ;;
            -e) expression=$2; shift 2 ;;
            *) shift ;;
        esac
    done
    [ "$file" = "$RUNTIME_FILE" ] || return 1
    case "$expression" in
        '@.tcpRules[*]') printf '203.0.113.0/24|*\n' ;;
        '@.udpRules[*]') printf '198.51.100.0/24|27000-27001\n' ;;
        '@.tcpDomains[*]') printf 'cdn.example.test|*\n' ;;
        '@.udpDomains[*]') printf 'voice.example.test|3074\n' ;;
        *) return 1 ;;
    esac
}
json_init() { :; }
json_add_int() { :; }
json_add_boolean() { :; }
json_add_string() { :; }
json_dump() { printf '{}\n'; }
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
logger() { :; }
config_value() {
    case "$1" in
        scope) printf 'lan\n' ;;
        target_ip) printf '\n' ;;
        openclash_mode) printf 'exclusive\n' ;;
        *) printf '%s\n' "${2:-}" ;;
    esac
}
port_hints() { tcp_ports='27015-27050'; udp_ports='3478,4379-4380'; return 0; }
collect_domain_ips() { printf '192.0.2.77\n'; }
proxy_service_running() { return 1; }
proxy_service_stop() { return 1; }
proxy_service_start() { return 1; }

if start_all > "$TEST_ROOT/start.out" 2> "$TEST_ROOT/start.err"; then
    printf 'mock engine unexpectedly remained running\n' >&2
    exit 1
fi
[ -s "$NFT_CAPTURE" ] || {
    printf 'start_all did not submit an nft script\n' >&2
    exit 1
}
'''


def build_nft_fixture() -> str:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        functions = root / "traffic-functions.sh"
        harness = root / "harness.sh"
        functions.write_text(production_functions())
        harness.write_text(textwrap.dedent(HARNESS))
        functions.chmod(0o700)
        harness.chmod(0o700)
        result = subprocess.run(
            ["/bin/sh", str(harness), str(functions), str(root)],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode:
            raise AssertionError(
                "production start_all fixture failed\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}\n"
                f"start stderr:\n{(root / 'start.err').read_text()}"
            )
        return (root / "biubiu-acc.nft").read_text()


class TrafficRuleTests(unittest.TestCase):
    def test_start_all_generates_ipv4_tproxy_rules_for_wildcard_and_port_rules(self) -> None:
        fixture = build_nft_fixture()
        tproxy_rules = [line for line in fixture.splitlines() if " tproxy " in line]

        self.assertGreaterEqual(len(tproxy_rules), 6)
        self.assertTrue(all("tproxy ip to" in line for line in tproxy_rules))
        self.assertIn(
            "ip daddr @provider_domain_tcp_1 meta l4proto tcp tproxy ip to :18080",
            fixture,
        )
        self.assertIn(
            "ip daddr 203.0.113.0/24 meta l4proto tcp tproxy ip to :18080",
            fixture,
        )
        self.assertIn(
            "ip daddr @provider_domain_udp_1 udp dport { 3074 } tproxy ip to :18081",
            fixture,
        )
        self.assertIn(
            "ip daddr 198.51.100.0/24 udp dport { 27000-27001 } tproxy ip to :18081",
            fixture,
        )
        self.assertIn(
            "tcp dport { 27015-27050 } tproxy ip to :18080",
            fixture,
        )
        self.assertIn(
            "udp dport { 3478,4379-4380 } tproxy ip to :18081",
            fixture,
        )
        self.assertNotIn("tproxy to :", fixture)


if __name__ == "__main__":
    if sys.argv[1:] == ["--print-nft-fixture"]:
        sys.stdout.write(build_nft_fixture())
    else:
        unittest.main()
