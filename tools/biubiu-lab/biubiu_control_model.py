#!/usr/bin/env python3
"""Network-free models for the observed biubiu acceleration control plane."""

from __future__ import annotations

import base64
import binascii
import copy
from dataclasses import dataclass, field
from enum import IntEnum
from ipaddress import IPv4Address, ip_address
import json
import secrets
import time
from typing import Any, Mapping, Sequence


GAME_LIST_ENDPOINT = "/api/ping-server.game.ns.gameListV2?df=adat&ver=1.0.0"
SEARCH_GAME_ENDPOINT = "/api/ping-server.game.ns.searchGame?df=adat&ver=1.0.0"
CHECK_SPEEDUP_ENDPOINT = (
    "/api/ping-server.biuvpn.game.checkSpeedup?df=adat&ver=1.0.0"
)
SPEEDUP_CONFIG_ENDPOINT = (
    "/api/ping-server.biuvpn.game.getSpeedupConfig?df=adat&ver=1.0.1"
)
SIGNAL_LOGIN_ENDPOINT = "/api/ping-signal.open.login.loginV2?df=adat&ver=1.0.0"
CHANNEL_TICKET_ENDPOINT = (
    "/api/ping-signal.open.auth.getChannelStV2?df=adat&ver=1.0.0"
)

CHANNEL_PROTOCOLS = frozenset({"TCP", "UDP", "ICMP"})
ENGINE_PROTOCOL_IDS = {"ICMP": 1, "TCP": 6, "UDP": 0x11}


class Platform(IntEnum):
    ANDROID = 2
    IOS = 3
    PC = 6
    SWITCH = 7
    PLAYSTATION = 8
    XBOX = 9
    STEAM_DECK = 10


@dataclass(frozen=True)
class AccountIdentity:
    uid: str = field(repr=False)
    service_ticket: str = field(repr=False)

    def __post_init__(self) -> None:
        if not _ascii_decimal(self.uid) or int(self.uid) <= 0:
            raise ValueError("invalid business user ID")
        if not isinstance(self.service_ticket, str) or not self.service_ticket:
            raise ValueError("service ticket must not be empty")

    @classmethod
    def from_session_record(cls, record: Mapping[str, Any]) -> "AccountIdentity":
        try:
            data = record["login"]["data"]
            uid = data["userBasicInfo"]["localId"]
            service_ticket = data["sessionInfo"]["sessionId"]
        except (KeyError, TypeError) as exc:
            raise ValueError("session record has no acceleration identity") from exc
        try:
            return cls(uid=uid, service_ticket=service_ticket)
        except ValueError as exc:
            raise ValueError("session record has an invalid acceleration identity") from exc


@dataclass(frozen=True)
class ControlRequest:
    endpoint: str
    body: dict[str, Any] = field(repr=False)


@dataclass(frozen=True)
class ChannelAuthorization:
    channel_address: str
    channel_ip: str
    channel_ticket: str = field(repr=False)
    data_channel_session_id: int = field(repr=False)
    expires_at: int
    port: int
    protocol: str
    secret_type: str = field(repr=False)

    @property
    def engine_protocol_id(self) -> int:
        return ENGINE_PROTOCOL_IDS[self.protocol]


@dataclass(frozen=True)
class BproxyAuthorization:
    token: bytes = field(repr=False)
    xor_marker: int = field(repr=False)
    tcp_session_id: int = field(repr=False)

    def summary(self) -> dict[str, Any]:
        return {
            "hasToken": bool(self.token),
            "hasXorMarker": bool(self.xor_marker),
            "hasTcpSession": bool(self.tcp_session_id),
        }


@dataclass(frozen=True)
class SignalAuthorization:
    signal_session_id: str = field(repr=False)
    token: str = field(repr=False)
    xor: str = field(repr=False)
    channels: tuple[ChannelAuthorization, ...]

    def bproxy_authorization(self) -> BproxyAuthorization:
        try:
            token = base64.b64decode(self.token, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise ValueError("token must be valid Base64") from exc
        if not token:
            raise ValueError("token must decode to a non-empty byte string")

        marker_bytes = self.xor.encode("utf-8")
        tcp_session_id = next(
            (
                channel.data_channel_session_id
                for channel in self.channels
                if channel.engine_protocol_id == ENGINE_PROTOCOL_IDS["TCP"]
            ),
            0,
        )
        return BproxyAuthorization(
            token=token,
            xor_marker=marker_bytes[0] if marker_bytes else 0,
            tcp_session_id=tcp_session_id,
        )

    def summary(self) -> dict[str, Any]:
        return {
            "channelCount": len(self.channels),
            "protocols": sorted({channel.protocol for channel in self.channels}),
            "hasSignalSession": bool(self.signal_session_id),
            "hasToken": bool(self.token),
            "hasXor": bool(self.xor),
        }


def _ascii_decimal(value: object) -> bool:
    return (
        isinstance(value, str)
        and bool(value)
        and all("0" <= character <= "9" for character in value)
    )


def new_request_id(now_ms: int | None = None, suffix: int | None = None) -> str:
    now_ms = int(time.time() * 1000) if now_ms is None else now_ms
    suffix = secrets.randbelow(9000) + 1000 if suffix is None else suffix
    if (
        isinstance(now_ms, bool)
        or not isinstance(now_ms, int)
        or isinstance(suffix, bool)
        or not isinstance(suffix, int)
        or now_ms < 1
        or suffix < 1000
        or suffix > 9999
    ):
        raise ValueError("invalid request ID components")
    return f"{now_ms}{suffix}"


def _positive_int(value: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"{name} must be a positive integer")
    return value


def _nonnegative_int(value: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{name} must be a non-negative integer")
    return value


def _bounded_int(value: object, name: str, minimum: int, maximum: int) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or value > maximum
    ):
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _string(
    value: object, name: str, *, required: bool = True, maximum: int = 4096
) -> str:
    if value is None and not required:
        return ""
    if not isinstance(value, str) or (required and not value):
        qualifier = "a non-empty" if required else "a"
        raise ValueError(f"{name} must be {qualifier} string")
    if len(value) > maximum:
        raise ValueError(f"{name} is too long")
    return value


def _channel_protocol(value: object, name: str) -> str:
    protocol = _string(value, name, maximum=8).upper()
    if protocol not in CHANNEL_PROTOCOLS:
        raise ValueError(f"{name} is unsupported")
    return protocol


def _channel_ip(value: object, name: str) -> str:
    text = _string(value, name, maximum=64)
    try:
        address = ip_address(text)
    except ValueError as exc:
        raise ValueError(f"{name} must be an IPv4 address") from exc
    if not isinstance(address, IPv4Address):
        raise ValueError(f"{name} must be an IPv4 address")
    return str(address)


def _authenticated_client(
    client_template: Mapping[str, Any], identity: AccountIdentity
) -> dict[str, Any]:
    if not isinstance(client_template, Mapping):
        raise ValueError("client template must be an object")
    client = copy.deepcopy(dict(client_template))
    extensions = client.get("ex")
    if extensions is None:
        extensions = {}
        client["ex"] = extensions
    if not isinstance(extensions, dict):
        raise ValueError("client.ex must be an object")
    for name, expected in (("st", identity.service_ticket), ("biuid", identity.uid)):
        current = extensions.get(name)
        if current not in (None, "", expected):
            raise ValueError(f"client.ex.{name} conflicts with the session")
        extensions[name] = expected
    return client


def _request(
    endpoint: str,
    data: Mapping[str, Any],
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    request_id: str | None,
) -> ControlRequest:
    if not endpoint.startswith("/api/") or "df=adat" not in endpoint:
        raise ValueError("control endpoint must use the ADAT API")
    request_id = new_request_id() if request_id is None else request_id
    if not _ascii_decimal(request_id) or len(request_id) > 32:
        raise ValueError("request ID must be a decimal string")
    client = _authenticated_client(client_template, identity)
    return ControlRequest(
        endpoint=endpoint,
        body={
            "data": copy.deepcopy(dict(data)),
            "id": request_id,
            "client": json.dumps(client, ensure_ascii=False, separators=(",", ":")),
        },
    )


def game_list_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    *,
    page: int = 1,
    size: int = 20,
    last_sort_key: str = "",
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(last_sort_key, str):
        raise ValueError("last sort key must be a string")
    data = {
        "page": {"page": _positive_int(page, "page"), "size": _positive_int(size, "size")},
        "lastSortKey": last_sort_key,
    }
    return _request(GAME_LIST_ENDPOINT, data, identity, client_template, request_id)


def search_game_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    keyword: str,
    *,
    page: int = 1,
    size: int = 20,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(keyword, str) or not keyword.strip():
        raise ValueError("search keyword must not be empty")
    data = {
        "keyword": keyword,
        "page": {"page": _positive_int(page, "page"), "size": _positive_int(size, "size")},
    }
    return _request(SEARCH_GAME_ENDPOINT, data, identity, client_template, request_id)


def check_speedup_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    game_id: int,
    area_id: int,
    *,
    polling: int = 0,
    space: int = 0,
    request_id: str | None = None,
) -> ControlRequest:
    data = {
        "gameId": _positive_int(game_id, "game ID"),
        "areaId": _positive_int(area_id, "area ID"),
        "polling": _nonnegative_int(polling, "polling"),
        "space": _nonnegative_int(space, "space"),
    }
    return _request(CHECK_SPEEDUP_ENDPOINT, data, identity, client_template, request_id)


def speedup_config_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    game_id: int,
    area_id: int,
    platform: Platform,
    *,
    package_request: Mapping[str, Any],
    space: int = 0,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(platform, Platform):
        raise ValueError("platform must be a known Platform value")
    if not isinstance(package_request, Mapping):
        raise ValueError("package request must be an object")
    data = {
        "gameId": _positive_int(game_id, "game ID"),
        "areaId": _positive_int(area_id, "area ID"),
        "space": _nonnegative_int(space, "space"),
        "platformId": int(platform),
        "pkgRequest": copy.deepcopy(dict(package_request)),
    }
    return _request(SPEEDUP_CONFIG_ENDPOINT, data, identity, client_template, request_id)


def signal_login_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    engine_client: Mapping[str, Any],
    channels: Sequence[Mapping[str, Any]],
    signal_ticket: str,
    *,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(engine_client, Mapping):
        raise ValueError("engine client must be an object")
    if not isinstance(signal_ticket, str) or not signal_ticket:
        raise ValueError("signal ticket must not be empty")
    channel_list: list[dict[str, Any]] = []
    for index, channel in enumerate(channels):
        name = f"channels[{index}]"
        if not isinstance(channel, Mapping):
            raise ValueError(f"{name} must be an object")
        channel_list.append(
            {
                "dataChannelIp": _channel_ip(
                    channel.get("dataChannelIp"), f"{name}.dataChannelIp"
                ),
                "port": _bounded_int(
                    channel.get("port"), f"{name}.port", 1, 65535
                ),
                "proType": _channel_protocol(
                    channel.get("proType"), f"{name}.proType"
                ),
            }
        )
    if not channel_list:
        raise ValueError("signal login requires at least one data channel")
    data = {
        "engineClient": copy.deepcopy(dict(engine_client)),
        "list": channel_list,
        "signalSt": signal_ticket,
    }
    return _request(SIGNAL_LOGIN_ENDPOINT, data, identity, client_template, request_id)


def _parse_channel_authorizations(
    raw_channels: object, list_name: str
) -> tuple[ChannelAuthorization, ...]:
    if not isinstance(raw_channels, list) or not raw_channels:
        raise ValueError(f"{list_name} must contain channels")

    channels: list[ChannelAuthorization] = []
    for index, raw_channel in enumerate(raw_channels):
        name = f"{list_name}[{index}]"
        if not isinstance(raw_channel, Mapping):
            raise ValueError(f"{name} must be an object")
        channels.append(
            ChannelAuthorization(
                channel_address=_string(
                    raw_channel.get("channelAddress", ""),
                    f"{name}.channelAddress",
                    required=False,
                    maximum=1024,
                ),
                channel_ip=_channel_ip(
                    raw_channel.get("channelIp"), f"{name}.channelIp"
                ),
                channel_ticket=_string(
                    raw_channel.get("channelSt"), f"{name}.channelSt"
                ),
                data_channel_session_id=_bounded_int(
                    raw_channel.get("dataChannelSessionId"),
                    f"{name}.dataChannelSessionId",
                    1,
                    0xFFFFFFFF,
                ),
                expires_at=_bounded_int(
                    raw_channel.get("expireTime"),
                    f"{name}.expireTime",
                    0,
                    0x7FFFFFFFFFFFFFFF,
                ),
                port=_bounded_int(
                    raw_channel.get("port"), f"{name}.port", 1, 65535
                ),
                protocol=_channel_protocol(
                    raw_channel.get("proType"), f"{name}.proType"
                ),
                secret_type=_string(
                    raw_channel.get("secretType"), f"{name}.secretType"
                ),
            )
        )
    return tuple(channels)


def parse_signal_authorization(value: object) -> SignalAuthorization:
    """Parse the direct `data` object returned by signal login."""

    if not isinstance(value, Mapping):
        raise ValueError("signal authorization must be an object")
    channels = _parse_channel_authorizations(
        value.get("channelAuthList"), "channelAuthList"
    )

    return SignalAuthorization(
        signal_session_id=_string(
            value.get("signalSessionId"), "signalSessionId"
        ),
        token=_string(value.get("token"), "token"),
        xor=_string(value.get("xor", ""), "xor", required=False),
        channels=channels,
    )


def parse_channel_ticket_data(value: object) -> tuple[ChannelAuthorization, ...]:
    """Parse the direct `data` object returned by channel-ticket renewal."""

    if not isinstance(value, Mapping):
        raise ValueError("channel ticket response must be an object")
    return _parse_channel_authorizations(
        value.get("dataChannelList"), "dataChannelList"
    )


def channel_ticket_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    engine_client: Mapping[str, Any],
    channels: Sequence[Mapping[str, Any]],
    *,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(engine_client, Mapping):
        raise ValueError("engine client must be an object")
    channel_list: list[dict[str, Any]] = []
    for index, channel in enumerate(channels):
        name = f"channels[{index}]"
        if not isinstance(channel, Mapping):
            raise ValueError(f"{name} must be an object")
        channel_list.append(
            {
                "channelIp": _channel_ip(
                    channel.get("channelIp"), f"{name}.channelIp"
                ),
                "dataChannelSessionId": _bounded_int(
                    channel.get("dataChannelSessionId"),
                    f"{name}.dataChannelSessionId",
                    1,
                    0xFFFFFFFF,
                ),
                "port": _bounded_int(
                    channel.get("port"), f"{name}.port", 1, 65535
                ),
                "proType": _channel_protocol(
                    channel.get("proType"), f"{name}.proType"
                ),
                "secretType": _string(
                    channel.get("secretType"), f"{name}.secretType"
                ),
                "type": _bounded_int(
                    channel.get("type"), f"{name}.type", 0, 0x7FFFFFFF
                ),
            }
        )
    if not channel_list:
        raise ValueError("channel ticket request requires at least one channel")
    data = {
        "engineClient": copy.deepcopy(dict(engine_client)),
        "channelAuthDTO": {"dataChannelList": channel_list},
    }
    return _request(
        CHANNEL_TICKET_ENDPOINT, data, identity, client_template, request_id
    )
