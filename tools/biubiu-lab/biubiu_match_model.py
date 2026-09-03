#!/usr/bin/env python3
"""Network-free model for compiling built-in game match selections."""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress
import json
import re
from typing import Iterable, Mapping, Sequence


GAME_ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
DOMAIN_PATTERN = re.compile(
    r"^(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)*"
    r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"
)
MATCH_MODES = {"provider-profile", "provider-profile-plus-hints"}
SCOPES = {"lan", "device"}


@dataclass(frozen=True, order=True)
class PortRange:
    start: int
    end: int

    @classmethod
    def parse(cls, value: object) -> "PortRange":
        if not isinstance(value, str) or not value:
            raise ValueError("port range must be a non-empty string")
        parts = value.split("-")
        if len(parts) not in (1, 2) or any(not part.isdecimal() for part in parts):
            raise ValueError("invalid port range")
        start = int(parts[0])
        end = int(parts[-1])
        if not 1 <= start <= end <= 65535:
            raise ValueError("port range is outside 1-65535")
        return cls(start, end)

    def render(self) -> str:
        return str(self.start) if self.start == self.end else f"{self.start}-{self.end}"


@dataclass(frozen=True)
class GameProfile:
    id: str
    name: str
    match_mode: str
    identity_dns_suffixes: tuple[str, ...]
    excluded_dns_suffixes: tuple[str, ...]
    tcp_destination_ports: tuple[PortRange, ...]
    udp_destination_ports: tuple[PortRange, ...]

    @classmethod
    def from_mapping(cls, value: object) -> "GameProfile":
        if not isinstance(value, Mapping):
            raise ValueError("game profile must be an object")
        profile_id = value.get("id")
        name = value.get("name")
        match_mode = value.get("match_mode")
        if not isinstance(profile_id, str) or not GAME_ID_PATTERN.fullmatch(profile_id):
            raise ValueError("invalid game profile id")
        if not isinstance(name, str) or not name.strip():
            raise ValueError("invalid game profile name")
        if match_mode not in MATCH_MODES:
            raise ValueError("invalid game profile match mode")
        return cls(
            id=profile_id,
            name=name.strip(),
            match_mode=match_mode,
            identity_dns_suffixes=_domains(value.get("identity_dns_suffixes")),
            excluded_dns_suffixes=_domains(value.get("excluded_dns_suffixes")),
            tcp_destination_ports=_ports(value.get("tcp_destination_ports")),
            udp_destination_ports=_ports(value.get("udp_destination_ports")),
        )


@dataclass(frozen=True)
class MatchPlan:
    scope: str
    source_cidr: str | None
    selected_games: tuple[str, ...]
    provider_profiles: tuple[str, ...]
    identity_dns_suffixes: tuple[str, ...]
    excluded_dns_suffixes: tuple[str, ...]
    tcp_destination_ports: tuple[PortRange, ...]
    udp_destination_ports: tuple[PortRange, ...]

    def as_mapping(self) -> dict[str, object]:
        return {
            "scope": self.scope,
            "source_cidr": self.source_cidr,
            "selected_games": list(self.selected_games),
            "provider_profiles": list(self.provider_profiles),
            "identity_dns_suffixes": list(self.identity_dns_suffixes),
            "excluded_dns_suffixes": list(self.excluded_dns_suffixes),
            "tcp_destination_ports": [
                port_range.render() for port_range in self.tcp_destination_ports
            ],
            "udp_destination_ports": [
                port_range.render() for port_range in self.udp_destination_ports
            ],
            "applied": False,
        }


def load_catalog(value: str | bytes | Mapping[str, object]) -> dict[str, GameProfile]:
    if isinstance(value, (str, bytes)):
        value = json.loads(value)
    if not isinstance(value, Mapping) or value.get("schema_version") != 1:
        raise ValueError("unsupported game catalog")
    raw_profiles = value.get("profiles")
    if not isinstance(raw_profiles, Sequence) or isinstance(raw_profiles, (str, bytes)):
        raise ValueError("catalog profiles must be an array")
    profiles: dict[str, GameProfile] = {}
    for raw_profile in raw_profiles:
        profile = GameProfile.from_mapping(raw_profile)
        if profile.id in profiles:
            raise ValueError("duplicate game profile id")
        if set(profile.identity_dns_suffixes) & set(profile.excluded_dns_suffixes):
            raise ValueError("identity and exclusion domains overlap")
        profiles[profile.id] = profile
    if not profiles:
        raise ValueError("game catalog is empty")
    return profiles


def compile_match_plan(
    catalog: Mapping[str, GameProfile],
    selected_games: Iterable[str],
    *,
    scope: str,
    target_ip: str | None = None,
) -> MatchPlan:
    if scope not in SCOPES:
        raise ValueError("invalid acceleration scope")
    source_cidr = None
    if scope == "device":
        try:
            source_cidr = f"{ipaddress.IPv4Address(target_ip)}/32"
        except ipaddress.AddressValueError as exc:
            raise ValueError("device scope requires a valid IPv4 address") from exc

    unique_games = tuple(dict.fromkeys(selected_games))
    if not unique_games:
        raise ValueError("at least one game must be selected")
    try:
        profiles = tuple(catalog[game_id] for game_id in unique_games)
    except KeyError as exc:
        raise ValueError("selected game is not in the catalog") from exc

    exclusions = _unique(
        domain for profile in profiles for domain in profile.excluded_dns_suffixes
    )
    excluded_set = set(exclusions)
    identities = tuple(
        domain
        for domain in _unique(
            domain for profile in profiles for domain in profile.identity_dns_suffixes
        )
        if domain not in excluded_set
    )
    return MatchPlan(
        scope=scope,
        source_cidr=source_cidr,
        selected_games=unique_games,
        provider_profiles=tuple(profile.id for profile in profiles),
        identity_dns_suffixes=identities,
        excluded_dns_suffixes=exclusions,
        tcp_destination_ports=_merge_ranges(
            port_range
            for profile in profiles
            if profile.match_mode == "provider-profile-plus-hints"
            for port_range in profile.tcp_destination_ports
        ),
        udp_destination_ports=_merge_ranges(
            port_range
            for profile in profiles
            if profile.match_mode == "provider-profile-plus-hints"
            for port_range in profile.udp_destination_ports
        ),
    )


def _domains(value: object) -> tuple[str, ...]:
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes)):
        raise ValueError("domain list must be an array")
    result: list[str] = []
    for domain in value:
        if not isinstance(domain, str):
            raise ValueError("domain suffix must be a string")
        normalized = domain.rstrip(".").lower()
        if not DOMAIN_PATTERN.fullmatch(normalized):
            raise ValueError("invalid domain suffix")
        if normalized not in result:
            result.append(normalized)
    return tuple(result)


def _ports(value: object) -> tuple[PortRange, ...]:
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes)):
        raise ValueError("port list must be an array")
    return tuple(PortRange.parse(port_range) for port_range in value)


def _unique(values: Iterable[str]) -> tuple[str, ...]:
    return tuple(dict.fromkeys(values))


def _merge_ranges(values: Iterable[PortRange]) -> tuple[PortRange, ...]:
    merged: list[PortRange] = []
    for current in sorted(set(values)):
        if not merged or current.start > merged[-1].end + 1:
            merged.append(current)
            continue
        previous = merged[-1]
        merged[-1] = PortRange(previous.start, max(previous.end, current.end))
    return tuple(merged)
