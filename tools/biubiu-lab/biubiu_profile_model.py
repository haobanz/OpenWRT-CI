#!/usr/bin/env python3
"""Network-free parser for the observed biubiu engine profile schema."""

from __future__ import annotations

from dataclasses import dataclass, field
from ipaddress import ip_address, ip_network
import json
from typing import Any, Mapping, Sequence


MAX_PROFILE_BYTES = 2 * 1024 * 1024
SUPPORTED_OUTBOUND_TYPES = frozenset(
    {"direct", "bolt", "blackhole", "bproxy", "mock", "bypath", "spare"}
)
SUPPORTED_ROUTE_MODES = frozenset(
    {"bolt", "direct", "blackhole", "mock", "next"}
)
SUPPORTED_CHANNEL_PROTOCOLS = frozenset({"TCP", "UDP", "ICMP"})


def _object(value: object, name: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{name} must be an object")
    return value


def _array(value: object, name: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{name} must be an array")
    return value


def _string(
    value: object, name: str, *, required: bool = True, max_length: int = 4096
) -> str:
    if value is None and not required:
        return ""
    if not isinstance(value, str) or (required and not value):
        raise ValueError(f"{name} must be a non-empty string")
    if len(value) > max_length:
        raise ValueError(f"{name} is too long")
    return value


def _integer(
    value: object,
    name: str,
    *,
    minimum: int = 0,
    maximum: int = 2**31 - 1,
) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or value > maximum
    ):
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _boolean(value: object, name: str, *, default: bool = False) -> bool:
    if value is None:
        return default
    if not isinstance(value, bool):
        raise ValueError(f"{name} must be a boolean")
    return value


def _ip(value: object, name: str, *, required: bool = True) -> str:
    text = _string(value, name, required=required, max_length=64)
    if not text:
        return text
    try:
        return str(ip_address(text))
    except ValueError as exc:
        raise ValueError(f"{name} must be an IP address") from exc


def _cidr(value: object, name: str) -> str:
    text = _string(value, name, max_length=64)
    try:
        return str(ip_network(text, strict=False))
    except ValueError as exc:
        raise ValueError(f"{name} must be an IP network") from exc


def _string_list(value: object, name: str) -> tuple[str, ...]:
    if value is None:
        return ()
    return tuple(
        _string(item, f"{name}[{index}]")
        for index, item in enumerate(_array(value, name))
    )


def _cidr_list(value: object, name: str) -> tuple[str, ...]:
    if value is None:
        return ()
    return tuple(
        _cidr(item, f"{name}[{index}]")
        for index, item in enumerate(_array(value, name))
    )


def _detail_object(value: object, name: str) -> Mapping[str, Any]:
    if isinstance(value, str):
        if len(value.encode("utf-8")) > MAX_PROFILE_BYTES:
            raise ValueError(f"{name} is too large")
        try:
            value = json.loads(value)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{name} is not valid JSON") from exc
    return _object(value, name)


@dataclass(frozen=True)
class BoltChannel:
    protocol: str
    address: str
    port: int
    isp: int = 0
    backup_address: str = ""
    backup_isp: int = 0
    tertiary_address: str = ""
    tertiary_isp: int = 0
    encrypted: bool = False
    client_parameter: str = field(default="", repr=False)
    server_parameter: str = field(default="", repr=False)
    strategy: str = field(default="", repr=False)

    @classmethod
    def from_mapping(cls, value: object, name: str) -> "BoltChannel":
        item = _object(value, name)
        protocol = _string(item.get("proType"), f"{name}.proType", max_length=8).upper()
        if protocol not in SUPPORTED_CHANNEL_PROTOCOLS:
            raise ValueError(f"{name}.proType is unsupported")
        return cls(
            protocol=protocol,
            address=_ip(item.get("ip"), f"{name}.ip"),
            port=_integer(item.get("port"), f"{name}.port", minimum=1, maximum=65535),
            isp=_integer(item.get("isp", 0), f"{name}.isp"),
            backup_address=_ip(item.get("bip"), f"{name}.bip", required=False),
            backup_isp=_integer(item.get("bisp", 0), f"{name}.bisp"),
            tertiary_address=_ip(item.get("cip"), f"{name}.cip", required=False),
            tertiary_isp=_integer(item.get("cisp", 0), f"{name}.cisp"),
            encrypted=_boolean(item.get("encryption"), f"{name}.encryption"),
            client_parameter=_string(
                item.get("bbCliParam"), f"{name}.bbCliParam", required=False
            ),
            server_parameter=_string(
                item.get("bbSrvParam"), f"{name}.bbSrvParam", required=False
            ),
            strategy=_string(
                item.get("bbstrategy"), f"{name}.bbstrategy", required=False
            ),
        )


@dataclass(frozen=True)
class Outbound:
    outbound_id: str
    outbound_type: str
    name: str
    channels: tuple[BoltChannel, ...] = ()
    detect_address: str = ""

    @classmethod
    def from_mapping(cls, value: object, name: str) -> "Outbound":
        item = _object(value, name)
        outbound_type = _string(
            item.get("type"), f"{name}.type", max_length=32
        ).lower()
        if outbound_type not in SUPPORTED_OUTBOUND_TYPES:
            raise ValueError(f"{name}.type is unsupported")

        channels: tuple[BoltChannel, ...] = ()
        detect_address = ""
        if outbound_type in {"bolt", "bypath", "spare"}:
            raw_detail = item.get("rawDetailConfig", item.get("config"))
            detail = _detail_object(raw_detail, f"{name}.rawDetailConfig")
            raw_channels = _array(
                detail.get("dataChannelList"),
                f"{name}.rawDetailConfig.dataChannelList",
            )
            channels = tuple(
                BoltChannel.from_mapping(channel, f"{name}.dataChannelList[{index}]")
                for index, channel in enumerate(raw_channels)
            )
            if not channels:
                raise ValueError(f"{name} has no data channel")
            protocols = [channel.protocol for channel in channels]
            if len(protocols) != len(set(protocols)):
                raise ValueError(f"{name} has duplicate data-channel protocols")
            detect_address = _ip(
                detail.get("detectIp"), f"{name}.detectIp", required=False
            )

        return cls(
            outbound_id=_string(item.get("id"), f"{name}.id", max_length=128),
            outbound_type=outbound_type,
            name=_string(
                item.get("name", item.get("podName", "")),
                f"{name}.name",
                required=False,
            ),
            channels=channels,
            detect_address=detect_address,
        )


@dataclass(frozen=True)
class RouteRule:
    route_id: int
    mode: str
    outbound_id: str
    selector_kind: str
    selectors: tuple[str, ...]
    protocol: int
    ports: tuple[str, ...]

    @classmethod
    def from_mapping(cls, value: object, name: str, fallback_id: int) -> "RouteRule":
        item = _object(value, name)
        mode = _string(item.get("mode"), f"{name}.mode", max_length=32).lower()
        if mode not in SUPPORTED_ROUTE_MODES:
            raise ValueError(f"{name}.mode is unsupported")

        selector_kind, selectors = _route_selectors(item, name)
        ports = _route_ports(item, name)
        route_id = _integer(item.get("id", fallback_id), f"{name}.id")
        outbound_id = item.get("bypathId") or item.get("outboundId")
        return cls(
            route_id=route_id,
            mode=mode,
            outbound_id=_string(outbound_id, f"{name}.outboundId", max_length=128),
            selector_kind=selector_kind,
            selectors=selectors,
            protocol=_integer(item.get("protocol", 0), f"{name}.protocol", maximum=255),
            ports=ports,
        )


def _route_selectors(item: Mapping[str, Any], name: str) -> tuple[str, tuple[str, ...]]:
    table_id = item.get("cidrTableId")
    if isinstance(table_id, str) and table_id:
        return "cidr_table", (_string(table_id, f"{name}.cidrTableId", max_length=128),)
    domains = item.get("domainList")
    if isinstance(domains, list) and domains:
        return "domain", _string_list(domains, f"{name}.domainList")
    domain = item.get("domain")
    if isinstance(domain, str) and domain:
        return "domain", (_string(domain, f"{name}.domain", max_length=253),)
    cidrs = item.get("cidrList")
    if isinstance(cidrs, list) and cidrs:
        return "cidr", _cidr_list(cidrs, f"{name}.cidrList")
    cidr = item.get("cidrIp")
    if isinstance(cidr, str) and cidr:
        return "cidr", (_cidr(cidr, f"{name}.cidrIp"),)
    raise ValueError(f"{name} has no route selector")


def _route_ports(item: Mapping[str, Any], name: str) -> tuple[str, ...]:
    values: list[object] = []
    if item.get("port") is not None:
        port = _integer(item["port"], f"{name}.port", maximum=65535)
        if port > 0:
            values.append(port)
    if item.get("portList") is not None:
        values.extend(_array(item["portList"], f"{name}.portList"))
    result: list[str] = []
    for index, value in enumerate(values):
        field_name = f"{name}.ports[{index}]"
        if isinstance(value, bool):
            raise ValueError(f"{field_name} is invalid")
        if isinstance(value, int):
            low = high = value
        elif isinstance(value, str):
            normalized = value.replace("~", "-", 1)
            parts = normalized.split("-", 1)
            if not all(part.isascii() and part.isdecimal() for part in parts):
                raise ValueError(f"{field_name} is invalid")
            low = int(parts[0])
            high = int(parts[-1])
        else:
            raise ValueError(f"{field_name} is invalid")
        if not 0 <= low <= 65535 or not 0 <= high <= 65535:
            raise ValueError(f"{field_name} is invalid")
        low, high = sorted((low, high))
        result.append(str(low) if low == high else f"{low}-{high}")
    return tuple(result)


@dataclass(frozen=True)
class TunProfile:
    local_networks: tuple[str, ...]
    include_networks: tuple[str, ...]
    exclude_networks: tuple[str, ...]
    dns_servers: tuple[str, ...]
    search_domains: tuple[str, ...]
    mtu: int

    @classmethod
    def from_mapping(cls, value: object) -> "TunProfile":
        item = _object(value, "inboundProfile")
        dns_servers = tuple(
            _ip(server, f"inboundProfile.dnsServerList[{index}]")
            for index, server in enumerate(
                _array(item.get("dnsServerList", []), "inboundProfile.dnsServerList")
            )
        )
        return cls(
            local_networks=_cidr_list(item.get("localIpList"), "inboundProfile.localIpList"),
            include_networks=_cidr_list(
                item.get("includeRouteList"), "inboundProfile.includeRouteList"
            ),
            exclude_networks=_cidr_list(
                item.get("excludeRouteList"), "inboundProfile.excludeRouteList"
            ),
            dns_servers=dns_servers,
            search_domains=_string_list(
                item.get("searchDomainList"), "inboundProfile.searchDomainList"
            ),
            mtu=_integer(item.get("mtu", 0), "inboundProfile.mtu", maximum=65535),
        )


@dataclass(frozen=True)
class SignalProfile:
    ticket: str = field(repr=False)
    heartbeat_interval: int
    heartbeat_max_interval: int
    max_ticket_refreshes: int

    @classmethod
    def from_mapping(cls, value: object) -> "SignalProfile":
        item = _object(value, "outboundProfile.signalConfig")
        heartbeat_interval = _integer(
            item.get("heartbeatInterval"), "signalConfig.heartbeatInterval"
        )
        heartbeat_max_interval = _integer(
            item.get("heartbeatMaxInterval"), "signalConfig.heartbeatMaxInterval"
        )
        max_ticket_refreshes = _integer(
            item.get("maxReGetStCount"),
            "signalConfig.maxReGetStCount",
            minimum=-(2**31),
        )
        return cls(
            ticket=_string(item.get("signalSt"), "signalConfig.signalSt"),
            heartbeat_interval=heartbeat_interval or 30,
            heartbeat_max_interval=heartbeat_max_interval or 120,
            max_ticket_refreshes=(
                max_ticket_refreshes if max_ticket_refreshes >= 0 else 2
            ),
        )


@dataclass(frozen=True)
class AccelerationProfile:
    tun: TunProfile
    outbounds: tuple[Outbound, ...]
    routes: tuple[RouteRule, ...]
    default_outbound_id: str
    signal: SignalProfile

    def summary(self) -> dict[str, Any]:
        protocols = sorted(
            {channel.protocol for outbound in self.outbounds for channel in outbound.channels}
        )
        return {
            "outboundCount": len(self.outbounds),
            "routeCount": len(self.routes),
            "channelProtocols": protocols,
            "hasSignalTicket": bool(self.signal.ticket),
            "includeNetworkCount": len(self.tun.include_networks),
            "excludeNetworkCount": len(self.tun.exclude_networks),
        }


def parse_acceleration_profile(value: object) -> AccelerationProfile:
    if isinstance(value, str):
        if len(value.encode("utf-8")) > MAX_PROFILE_BYTES:
            raise ValueError("profile is too large")
        try:
            value = json.loads(value)
        except json.JSONDecodeError as exc:
            raise ValueError("profile is not valid JSON") from exc
    root = _object(value, "profile")
    tun = TunProfile.from_mapping(root.get("inboundProfile"))
    outbound_profile = _object(root.get("outboundProfile"), "outboundProfile")
    outbounds = tuple(
        Outbound.from_mapping(item, f"outboundProfile.outboundConfigList[{index}]")
        for index, item in enumerate(
            _array(
                outbound_profile.get("outboundConfigList"),
                "outboundProfile.outboundConfigList",
            )
        )
    )
    outbound_ids = [outbound.outbound_id for outbound in outbounds]
    if not outbounds or len(outbound_ids) != len(set(outbound_ids)):
        raise ValueError("outbound IDs must be non-empty and unique")

    router_profile = _object(root.get("routerProfile"), "routerProfile")
    routes = tuple(
        RouteRule.from_mapping(item, f"routerProfile.routeList[{index}]", index)
        for index, item in enumerate(
            _array(router_profile.get("routeList"), "routerProfile.routeList")
        )
    )
    if not routes:
        raise ValueError("profile has no route")
    unknown_outbounds = sorted(
        {route.outbound_id for route in routes if route.outbound_id not in outbound_ids}
    )
    if unknown_outbounds:
        raise ValueError("route references an unknown outbound")
    default_outbound_id = _string(
        router_profile.get("defaultOutboundId"),
        "routerProfile.defaultOutboundId",
        max_length=128,
    )
    if default_outbound_id not in outbound_ids:
        raise ValueError("default outbound does not exist")
    signal = SignalProfile.from_mapping(outbound_profile.get("signalConfig"))
    return AccelerationProfile(
        tun=tun,
        outbounds=outbounds,
        routes=routes,
        default_outbound_id=default_outbound_id,
        signal=signal,
    )
