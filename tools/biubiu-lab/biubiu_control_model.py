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
PC_CHECK_SPEEDUP_ENDPOINT = (
    "/api/ping-server.biuvpn.game.checkSpeedup?ver=1.0.0"
)
PC_GAME_MAP_ENDPOINT = "/api/ping-server.game.pc.map?ver=1.0.0"
SPEEDUP_CONFIG_ENDPOINT = (
    "/api/ping-server.biuvpn.game.getSpeedupConfig?df=adat&ver=1.0.1"
)
SIGNAL_LOGIN_ENDPOINT = "/api/ping-signal.open.login.loginV2?ver=1.0.0&df=adat"
CHANNEL_TICKET_ENDPOINT = (
    "/api/ping-signal.open.auth.getChannelStV2?ver=1.0.0&df=adat"
)

CHANNEL_PROTOCOLS = frozenset({"TCP", "UDP", "ICMP"})
ENGINE_PROTOCOL_IDS = {"ICMP": 1, "TCP": 6, "UDP": 0x11}


class Platform(IntEnum):
    ALL = 0
    UNDEFINED = 1
    ANDROID = 2
    IOS = 3
    SIMULATOR = 4
    PC_WEB = 5
    PC = 6
    SWITCH = 7
    PLAYSTATION = 8
    XBOX = 9
    STEAM_DECK = 10


PC_PLATFORM_NAMES = {
    Platform.ALL: "all",
    Platform.UNDEFINED: "undefine",
    Platform.ANDROID: "android",
    Platform.IOS: "ios",
    Platform.SIMULATOR: "simulator",
    Platform.PC_WEB: "pcweb",
    Platform.PC: "pc",
    Platform.SWITCH: "switch",
    Platform.XBOX: "xbox",
    Platform.PLAYSTATION: "ps",
    Platform.STEAM_DECK: "steamdeck",
}
PC_ACCELERATION_MODES = frozenset({3, 4, 5})
PC_FALLBACK_MODES = (3, 5)


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
    *,
    native: bool = False,
) -> ControlRequest:
    if not endpoint.startswith("/api/"):
        raise ValueError("control endpoint must use the API namespace")
    if native:
        if "?ver=" not in endpoint or "df=adat" in endpoint:
            raise ValueError("native endpoint must defer the ADAT query parameter")
    elif "df=adat" not in endpoint:
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


def pc_check_speedup_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    game_id: int,
    area_id: int,
    *,
    polling: int = 0,
    use_member_speedup_experience: bool = False,
    last_jitter_time: int | None = None,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(use_member_speedup_experience, bool):
        raise ValueError("member speedup experience flag must be boolean")
    data: dict[str, Any] = {
        "gameId": _positive_int(game_id, "game ID"),
        "areaId": _positive_int(area_id, "area ID"),
        "polling": _nonnegative_int(polling, "polling"),
        "useMemberSpeedUpExperience": use_member_speedup_experience,
    }
    if last_jitter_time is not None:
        data["lastJitterTime"] = _nonnegative_int(
            last_jitter_time, "last jitter time"
        )
    return _request(
        PC_CHECK_SPEEDUP_ENDPOINT,
        data,
        identity,
        client_template,
        request_id,
        native=True,
    )


def pc_game_map_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    game_ids: Sequence[int],
    *,
    request_id: str | None = None,
) -> ControlRequest:
    if isinstance(game_ids, (str, bytes)) or not isinstance(game_ids, Sequence):
        raise ValueError("game IDs must be a sequence")
    normalized = [_positive_int(game_id, "game ID") for game_id in game_ids]
    if not normalized or len(normalized) > 50:
        raise ValueError("game map accepts between 1 and 50 game IDs")
    return _request(
        PC_GAME_MAP_ENDPOINT,
        {"gameIds": normalized},
        identity,
        client_template,
        request_id,
        native=True,
    )


def pc_start_metadata(
    game: Mapping[str, Any],
    game_id: int,
    area_id: int,
    *,
    selected_mode: int | None = None,
) -> dict[str, Any]:
    if not isinstance(game, Mapping):
        raise ValueError("game map entry must be an object")
    game_id = _positive_int(game_id, "game ID")
    area_id = _positive_int(area_id, "area ID")
    game_info = game.get("gameInfo")
    areas = game.get("areaList")
    if not isinstance(game_info, Mapping) or not isinstance(areas, Sequence):
        raise ValueError("game map entry has no game or area metadata")
    if game_info.get("gameId") != game_id:
        raise ValueError("game map entry does not match the game ID")
    platform_value = game_info.get("platformId")
    if isinstance(platform_value, bool) or not isinstance(platform_value, int):
        raise ValueError("game map entry has an invalid platform ID")
    try:
        platform = Platform(platform_value)
    except ValueError as exc:
        raise ValueError("game map entry has an unsupported platform ID") from exc
    if not any(
        isinstance(area, Mapping) and area.get("areaId") == area_id
        for area in areas
    ):
        raise ValueError("game map entry does not contain the area ID")

    raw_modes = game.get("speedupModelList")
    if raw_modes is None:
        raw_modes = []
    if isinstance(raw_modes, (str, bytes)) or not isinstance(raw_modes, Sequence):
        raise ValueError("speedup model list must be an array")
    modes: list[int] = []
    for entry in raw_modes:
        if not isinstance(entry, Mapping):
            raise ValueError("speedup model entry must be an object")
        mode = entry.get("speedupModelId")
        if isinstance(mode, bool) or not isinstance(mode, int) or mode not in PC_ACCELERATION_MODES:
            raise ValueError("speedup model entry has an unsupported ID")
        if mode not in modes:
            modes.append(mode)
    if not modes:
        modes = list(PC_FALLBACK_MODES)
    if selected_mode is None:
        selected_mode = modes[0]
    if (
        isinstance(selected_mode, bool)
        or not isinstance(selected_mode, int)
        or selected_mode not in modes
    ):
        raise ValueError("selected speedup model is not available")
    ordered_modes = [selected_mode] + [mode for mode in modes if mode != selected_mode]
    return {
        "gameId": game_id,
        "gameArea": area_id,
        "serverId": 0,
        "accMode": selected_mode,
        "accModeList": ordered_modes,
        "accPodId": "auto",
        "gamePlatform": PC_PLATFORM_NAMES[platform],
        "gamePlatformId": int(platform),
    }


def speedup_config_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    game_id: int,
    area_id: int,
    platform: Platform,
    *,
    package_request: Mapping[str, Any],
    space: int = 0,
    client_session_id: str = "",
    scout_path_result: Mapping[str, Any] | None = None,
    optimize_mode: int = 0,
    dual_net_online: int = 0,
    request_id: str | None = None,
) -> ControlRequest:
    if not isinstance(platform, Platform):
        raise ValueError("platform must be a known Platform value")
    if not isinstance(package_request, Mapping):
        raise ValueError("package request must be an object")
    if not isinstance(client_session_id, str) or len(client_session_id) > 256:
        raise ValueError("client session ID must be a string")
    if scout_path_result is None:
        scout_path_result = {"strategyId": "", "detectResult": []}
    if not isinstance(scout_path_result, Mapping):
        raise ValueError("scout path result must be an object")
    optimize_mode = _nonnegative_int(optimize_mode, "optimize mode")
    if dual_net_online not in (0, 1):
        raise ValueError("dual net online must be 0 or 1")
    data = {
        "gameId": _positive_int(game_id, "game ID"),
        "areaId": _positive_int(area_id, "area ID"),
        "space": _nonnegative_int(space, "space"),
        "platformId": int(platform),
        "clientSessionId": client_session_id,
        "scoutPathResult": copy.deepcopy(dict(scout_path_result)),
        "optimizeMode": optimize_mode,
        "dualNetOnline": dual_net_online,
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
    channel_list = _signal_login_channels(channels)
    data = {
        "engineClient": copy.deepcopy(dict(engine_client)),
        "list": channel_list,
        "signalSt": signal_ticket,
    }
    return _request(SIGNAL_LOGIN_ENDPOINT, data, identity, client_template, request_id)


def _signal_login_channels(
    channels: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
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
    return channel_list


def pc_signal_login_request(
    identity: AccountIdentity,
    client_template: Mapping[str, Any],
    engine_client: Mapping[str, Any],
    channels: Sequence[Mapping[str, Any]],
    signal_ticket: str,
) -> ControlRequest:
    """Build the native Windows loginV2 payload in serializer order."""

    if not isinstance(engine_client, Mapping):
        raise ValueError("engine client must be an object")
    if not isinstance(signal_ticket, str) or not signal_ticket:
        raise ValueError("signal ticket must not be empty")
    client = _authenticated_client(client_template, identity)
    data = {
        "signalSt": signal_ticket,
        "engineClient": copy.deepcopy(dict(engine_client)),
        "list": _signal_login_channels(channels),
    }
    return ControlRequest(
        endpoint=SIGNAL_LOGIN_ENDPOINT,
        body={
            "client": json.dumps(client, ensure_ascii=False, separators=(",", ":")),
            "data": data,
            # The desktop service leaves ClientData.id zero-initialized. The
            # selected accPodId is part of the separate start configuration.
            "id": "",
        },
    )


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
