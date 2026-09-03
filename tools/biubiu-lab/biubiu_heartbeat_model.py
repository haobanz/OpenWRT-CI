#!/usr/bin/env python3
"""Network-free model of the observed direct signal heartbeat contract."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass, field
from ipaddress import IPv4Address, ip_address
import json
from typing import Any, Mapping
from urllib.parse import urlsplit

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.padding import PKCS7


AES_SIZE = 16
HEARTBEAT_ENDPOINT = "/api/open.heartbeat.heartbeatV2"
HEARTBEAT_HEADER = "x-biu-client"
HEARTBEAT_CALLER = "ping_android"
HEARTBEAT_CONTENT_TYPE = "application/json"
HEARTBEAT_SUCCESS_CODE = 2_000_000
CONSOLE_PLATFORM_ID = 7
CHANNEL_PROTOCOLS = frozenset({"TCP", "UDP", "ICMP"})


@dataclass(frozen=True)
class HeartbeatCipherMaterial:
    key: bytes = field(repr=False)
    iv: bytes = field(repr=False)

    def __post_init__(self) -> None:
        if not isinstance(self.key, bytes) or not isinstance(self.iv, bytes):
            raise ValueError("heartbeat AES key and IV must be bytes")
        if len(self.key) != AES_SIZE or len(self.iv) != AES_SIZE:
            raise ValueError("heartbeat requires a 16-byte AES key and IV")


@dataclass(frozen=True)
class HeartbeatRequest:
    method: str
    url: str
    headers: Mapping[str, str] = field(repr=False)
    body: bytes = field(repr=False)


@dataclass(frozen=True)
class HeartbeatChannel:
    protocol: str
    ip: str
    port: int

    @property
    def endpoint(self) -> str:
        return f"{self.protocol}://{self.ip}:{self.port}"


@dataclass(frozen=True)
class HeartbeatResult:
    code: int
    message: str = field(repr=False)
    state: int | None
    channels: tuple[HeartbeatChannel, ...]

    @property
    def successful(self) -> bool:
        return self.code == HEARTBEAT_SUCCESS_CODE

    @property
    def endpoints(self) -> tuple[str, ...]:
        return tuple(channel.endpoint for channel in self.channels)


def _integer(
    value: object, name: str, *, minimum: int = 0, maximum: int = 0x7FFFFFFF
) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or value > maximum
    ):
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def _string(value: object, name: str, *, maximum: int = 4096) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{name} must be a non-empty string")
    if len(value) > maximum:
        raise ValueError(f"{name} is too long")
    return value


def _json_object(value: object, name: str) -> dict[str, Any]:
    if isinstance(value, Mapping):
        return dict(value)
    if isinstance(value, bytes):
        try:
            value = value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError(f"{name} is not UTF-8") from exc
    if not isinstance(value, str):
        raise ValueError(f"{name} must be a JSON object")
    try:
        parsed = json.loads(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} is not valid JSON") from exc
    if not isinstance(parsed, dict):
        raise ValueError(f"{name} must be a JSON object")
    return parsed


def _compact_json(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def build_engine_client(
    version_name: str,
    version_code: int,
    channel: str,
    network: str,
    api_level: int,
    build_code: str,
) -> str:
    """Build the JSON string used in both the payload and x-biu-client."""

    value = {
        "ver": _string(version_name, "version name", maximum=128),
        "versionCode": _integer(version_code, "version code", minimum=1),
        "os": "android",
        "ch": _string(channel, "channel", maximum=128),
        "network": _string(network, "network", maximum=128),
        "apiLevel": _integer(api_level, "API level", minimum=1, maximum=1000),
        "build": _string(build_code, "build code", maximum=128),
    }
    return _compact_json(value)


def build_heartbeat_plaintext(
    uid: int,
    signal_session_id: str,
    platform_id: int,
    game_id: int,
    area_id: int,
    engine_version: str,
    engine_client: str,
) -> bytes:
    """Build the inner UTF-8 JSON before the protected heartbeat cipher."""

    client = _string(engine_client, "engine client", maximum=8192)
    _json_object(client, "engine client")
    value = {
        "uid": _integer(uid, "uid", minimum=1, maximum=0x7FFFFFFFFFFFFFFF),
        "type": 5 if platform_id == CONSOLE_PLATFORM_ID else 1,
        "appId": "biubiu",
        "engineVersion": _string(
            engine_version, "engine version", maximum=128
        ),
        "signalSessionId": _string(
            signal_session_id, "signal session ID", maximum=4096
        ),
        "gameId": str(_integer(game_id, "game ID", minimum=1)),
        "areaId": str(_integer(area_id, "area ID")),
        "engineClient": client,
    }
    return _compact_json(value).encode("utf-8")


def encrypt_heartbeat_payload(
    plaintext: bytes, material: HeartbeatCipherMaterial
) -> str:
    """Apply AES-128-CBC/PKCS7 and Android Base64.DEFAULT formatting."""

    if not isinstance(plaintext, bytes) or not plaintext:
        raise ValueError("heartbeat plaintext must be non-empty bytes")
    padder = PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(
        algorithms.AES(material.key), modes.CBC(material.iv)
    ).encryptor()
    ciphertext = encryptor.update(padded) + encryptor.finalize()
    return base64.encodebytes(ciphertext).decode("ascii")


def decrypt_heartbeat_payload(
    encoded: str, material: HeartbeatCipherMaterial
) -> bytes:
    """Reverse the heartbeat envelope while accepting Base64 whitespace."""

    value = _string(encoded, "heartbeat ciphertext", maximum=4 * 1024 * 1024)
    compact = "".join(value.split())
    try:
        ciphertext = base64.b64decode(compact, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ValueError("heartbeat ciphertext is not valid Base64") from exc
    if not ciphertext or len(ciphertext) % AES_SIZE:
        raise ValueError("invalid heartbeat ciphertext length")
    decryptor = Cipher(
        algorithms.AES(material.key), modes.CBC(material.iv)
    ).decryptor()
    padded = decryptor.update(ciphertext) + decryptor.finalize()
    unpadder = PKCS7(128).unpadder()
    try:
        return unpadder.update(padded) + unpadder.finalize()
    except ValueError as exc:
        raise ValueError("invalid heartbeat response padding") from exc


def build_heartbeat_request(
    host: str,
    encrypted_data: str,
    engine_client: str,
    *,
    scheme: str = "https",
) -> HeartbeatRequest:
    """Build a POST description without opening a network connection."""

    host = _string(host, "heartbeat host", maximum=253)
    scheme = _string(scheme, "heartbeat scheme", maximum=8).lower()
    if scheme not in {"http", "https"}:
        raise ValueError("heartbeat scheme must be HTTP or HTTPS")
    parsed = urlsplit(f"{scheme}://{host}")
    try:
        port = parsed.port
    except ValueError as exc:
        raise ValueError("heartbeat host has an invalid port") from exc
    if (
        not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("heartbeat host must not contain credentials or a path")
    if port is not None and not 1 <= port <= 65535:
        raise ValueError("heartbeat host has an invalid port")

    client = _string(engine_client, "engine client", maximum=8192)
    _json_object(client, "engine client")
    encrypted = _string(
        encrypted_data, "heartbeat ciphertext", maximum=4 * 1024 * 1024
    )
    try:
        decoded = base64.b64decode("".join(encrypted.split()), validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ValueError("heartbeat ciphertext is not valid Base64") from exc
    if not decoded or len(decoded) % AES_SIZE:
        raise ValueError("invalid heartbeat ciphertext length")

    body = _compact_json(
        {"caller": HEARTBEAT_CALLER, "data": encrypted}
    ).encode("utf-8")
    return HeartbeatRequest(
        method="POST",
        url=f"{scheme}://{host}{HEARTBEAT_ENDPOINT}",
        headers={
            "Content-Type": HEARTBEAT_CONTENT_TYPE,
            HEARTBEAT_HEADER: client,
        },
        body=body,
    )


def parse_heartbeat_response(value: object) -> HeartbeatResult:
    """Parse a decrypted heartbeat response into its channel endpoints."""

    response = _json_object(value, "heartbeat response")
    code = _integer(response.get("code"), "heartbeat response code")
    message_value = response.get("msg", "")
    if not isinstance(message_value, str):
        raise ValueError("heartbeat response message must be a string")

    data = response.get("data")
    if code != HEARTBEAT_SUCCESS_CODE and data is None:
        return HeartbeatResult(code, message_value, None, ())
    if not isinstance(data, Mapping):
        raise ValueError("heartbeat response data must be an object")

    state_value = data.get("state")
    if code == HEARTBEAT_SUCCESS_CODE and state_value is None:
        raise ValueError("missing state in heartbeat response")
    state = (
        None
        if state_value is None
        else _integer(state_value, "heartbeat state")
    )

    channels_value = data.get("dataChannelList", [])
    if channels_value is None:
        channels_value = []
    if not isinstance(channels_value, list):
        raise ValueError("heartbeat channel list must be an array")
    channels: list[HeartbeatChannel] = []
    for index, item in enumerate(channels_value):
        if not isinstance(item, Mapping):
            raise ValueError(f"heartbeat channel {index} must be an object")
        protocol = _string(
            item.get("proType"), f"heartbeat channel {index} protocol", maximum=8
        ).upper()
        if protocol not in CHANNEL_PROTOCOLS:
            raise ValueError(f"heartbeat channel {index} protocol is unsupported")
        raw_ip = _string(
            item.get("channelIp"), f"heartbeat channel {index} IP", maximum=64
        )
        try:
            address = ip_address(raw_ip)
        except ValueError as exc:
            raise ValueError(f"heartbeat channel {index} IP is invalid") from exc
        if not isinstance(address, IPv4Address):
            raise ValueError(f"heartbeat channel {index} IP must be IPv4")
        port = _integer(
            item.get("port"),
            f"heartbeat channel {index} port",
            minimum=1,
            maximum=65535,
        )
        channels.append(HeartbeatChannel(protocol, str(address), port))

    return HeartbeatResult(code, message_value, state, tuple(channels))


def parse_encrypted_heartbeat_response(
    encoded: str, material: HeartbeatCipherMaterial
) -> HeartbeatResult:
    return parse_heartbeat_response(decrypt_heartbeat_payload(encoded, material))
