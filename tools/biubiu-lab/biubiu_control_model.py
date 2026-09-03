#!/usr/bin/env python3
"""Network-free models for the observed biubiu acceleration control plane."""

from __future__ import annotations

import copy
from dataclasses import dataclass, field
from enum import IntEnum
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
    for channel in channels:
        if not isinstance(channel, Mapping):
            raise ValueError("channel must be an object")
        address = channel.get("dataChannelIp")
        protocol = channel.get("proType")
        port = channel.get("port")
        if not isinstance(address, str) or not address:
            raise ValueError("channel address must not be empty")
        if not isinstance(protocol, str) or not protocol:
            raise ValueError("channel protocol must not be empty")
        if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
            raise ValueError("channel port is invalid")
        channel_list.append(
            {"dataChannelIp": address, "port": port, "proType": protocol}
        )
    if not channel_list:
        raise ValueError("signal login requires at least one data channel")
    data = {
        "engineClient": copy.deepcopy(dict(engine_client)),
        "list": channel_list,
        "signalSt": signal_ticket,
    }
    return _request(SIGNAL_LOGIN_ENDPOINT, data, identity, client_template, request_id)
