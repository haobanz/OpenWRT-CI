#!/usr/bin/env python3
"""Network-free model of the observed biubiu Bolt v3 frame boundary."""

from __future__ import annotations

from dataclasses import dataclass, field
from ipaddress import IPv4Address, ip_address
import struct
from typing import Iterable


PROTOCOL_VERSION = 3
DATA_HEADER_LENGTH = 11

COMMAND_DATA = 0x11
COMMAND_CONNECT_REQUEST = 0x22
COMMAND_CONNECT_RESPONSE = 0x23
COMMAND_ASSOCIATE_REQUEST = 0x24
COMMAND_ASSOCIATE_RESPONSE = 0x25
COMMAND_CLOSE = 0x26
COMMAND_ERROR = 0x27

STATUS_SUCCESS = 0x22

REQUEST_COMMANDS = frozenset({COMMAND_CONNECT_REQUEST, COMMAND_ASSOCIATE_REQUEST})
STATUS_COMMANDS = frozenset(
    {COMMAND_CONNECT_RESPONSE, COMMAND_ASSOCIATE_RESPONSE, COMMAND_ERROR}
)
ENDPOINT_EXTENSION_TYPES = frozenset({1, 2, 7})


def _u8(value: object, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise ValueError(f"{name} must be an unsigned 8-bit integer")
    return value


def _u16(value: object, name: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 <= value <= 0xFFFF
    ):
        raise ValueError(f"{name} must be an unsigned 16-bit integer")
    return value


def _u32(value: object, name: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 <= value <= 0xFFFFFFFF
    ):
        raise ValueError(f"{name} must be an unsigned 32-bit integer")
    return value


def _bytes(value: object, name: str) -> bytes:
    if not isinstance(value, bytes):
        raise ValueError(f"{name} must be bytes")
    return value


@dataclass(frozen=True)
class Endpoint:
    address: str
    port: int

    def __post_init__(self) -> None:
        try:
            address = ip_address(self.address)
        except ValueError as exc:
            raise ValueError("endpoint address must be an IPv4 address") from exc
        if not isinstance(address, IPv4Address):
            raise ValueError("endpoint address must be an IPv4 address")
        object.__setattr__(self, "address", str(address))
        _u16(self.port, "endpoint port")
        if self.port == 0:
            raise ValueError("endpoint port must not be zero")


@dataclass(frozen=True)
class Extension:
    type_id: int
    value: bytes = field(repr=False)

    def __post_init__(self) -> None:
        _u8(self.type_id, "extension type")
        value = _bytes(self.value, "extension value")
        if len(value) > 0xFF:
            raise ValueError("extension value is too long")

    @classmethod
    def endpoint(cls, type_id: int, endpoint: Endpoint) -> "Extension":
        if type_id not in ENDPOINT_EXTENSION_TYPES:
            raise ValueError("endpoint extension type must be 1, 2, or 7")
        if not isinstance(endpoint, Endpoint):
            raise ValueError("endpoint must be an Endpoint")
        value = IPv4Address(endpoint.address).packed + struct.pack(">H", endpoint.port)
        return cls(type_id=type_id, value=value)

    def as_endpoint(self) -> Endpoint:
        if self.type_id not in ENDPOINT_EXTENSION_TYPES or len(self.value) != 6:
            raise ValueError("extension is not an observed endpoint extension")
        address = str(IPv4Address(self.value[:4]))
        port = struct.unpack(">H", self.value[4:])[0]
        return Endpoint(address, port)


@dataclass(frozen=True)
class V3Request:
    command: int
    session_id: int
    extensions: tuple[Extension, ...]
    payload: bytes = field(default=b"", repr=False)
    flags: int = 0


@dataclass(frozen=True)
class V3Response:
    command: int
    session_id: int
    connection_id: int
    status: int | None
    extensions: tuple[Extension, ...]
    payload: bytes = field(default=b"", repr=False)
    flags: int = 0

    @property
    def successful(self) -> bool:
        return self.command in {
            COMMAND_CONNECT_RESPONSE,
            COMMAND_ASSOCIATE_RESPONSE,
        } and self.status == STATUS_SUCCESS and self.connection_id != 0


@dataclass(frozen=True)
class V3Data:
    session_id: int
    connection_id: int
    payload: bytes = field(default=b"", repr=False)
    flags: int = 0


def _extension_bytes(
    extensions: Iterable[Extension],
) -> tuple[tuple[Extension, ...], bytes]:
    normalized = tuple(extensions)
    if len(normalized) > 0xFF:
        raise ValueError("too many extensions")
    encoded = bytearray()
    for extension in normalized:
        if not isinstance(extension, Extension):
            raise ValueError("extensions must contain Extension values")
        encoded.extend((extension.type_id, len(extension.value)))
        encoded.extend(extension.value)
    return normalized, bytes(encoded)


def encode_v3_request(
    command: int,
    session_id: int,
    extensions: Iterable[Extension] = (),
    *,
    payload: bytes = b"",
) -> bytes:
    """Encode the request shape observed for connect and UDP associate."""

    command = _u8(command, "command")
    if command not in REQUEST_COMMANDS:
        raise ValueError("unsupported Bolt v3 request command")
    session_id = _u32(session_id, "session ID")
    payload = _bytes(payload, "payload")
    normalized, encoded_extensions = _extension_bytes(extensions)

    header_length = 10 + len(encoded_extensions)
    total_length = header_length + len(payload)
    if header_length > 0xFF:
        raise ValueError("Bolt v3 header is too long")
    if total_length > 0xFFFF:
        raise ValueError("Bolt v3 frame is too long")

    header = struct.pack(
        ">BBHBIB",
        PROTOCOL_VERSION,
        header_length,
        total_length,
        command,
        session_id,
        len(normalized),
    )
    return header + encoded_extensions + payload


def encode_v3_data(session_id: int, connection_id: int, payload: bytes) -> bytes:
    """Encode the fixed 11-byte header used by observed v3 data frames."""

    session_id = _u32(session_id, "session ID")
    connection_id = _u16(connection_id, "connection ID")
    payload = _bytes(payload, "payload")
    total_length = DATA_HEADER_LENGTH + len(payload)
    if total_length > 0xFFFF:
        raise ValueError("Bolt v3 frame is too long")
    return struct.pack(
        ">BBHBIH",
        PROTOCOL_VERSION,
        DATA_HEADER_LENGTH,
        total_length,
        COMMAND_DATA,
        session_id,
        connection_id,
    ) + payload


def _frame(value: object, minimum_header: int) -> tuple[bytes, int, int, int]:
    data = _bytes(value, "frame")
    if len(data) < minimum_header:
        raise ValueError("Bolt v3 frame is truncated")
    version = data[0] & 0x0F
    flags = data[0] >> 4
    if version != PROTOCOL_VERSION:
        raise ValueError("unsupported Bolt protocol version")
    header_length = data[1]
    total_length = struct.unpack_from(">H", data, 2)[0]
    if header_length < minimum_header or header_length > total_length:
        raise ValueError("invalid Bolt v3 header length")
    if total_length != len(data):
        raise ValueError("Bolt v3 total length does not match the frame")
    return data, flags, header_length, total_length


def _parse_extensions(
    data: bytes, cursor: int, header_length: int, count: int
) -> tuple[Extension, ...]:
    extensions: list[Extension] = []
    for _ in range(count):
        if cursor + 2 > header_length:
            raise ValueError("Bolt v3 extension header is truncated")
        type_id, length = data[cursor], data[cursor + 1]
        cursor += 2
        end = cursor + length
        if end > header_length:
            raise ValueError("Bolt v3 extension value is truncated")
        extensions.append(Extension(type_id, data[cursor:end]))
        cursor = end
    if cursor != header_length:
        raise ValueError("Bolt v3 extension count does not cover the header")
    return tuple(extensions)


def parse_v3_request(value: object) -> V3Request:
    data, flags, header_length, total_length = _frame(value, 10)
    command = data[4]
    if command not in REQUEST_COMMANDS:
        raise ValueError("unsupported Bolt v3 request command")
    session_id = struct.unpack_from(">I", data, 5)[0]
    count = data[9]
    extensions = _parse_extensions(data, 10, header_length, count)
    return V3Request(
        command=command,
        session_id=session_id,
        extensions=extensions,
        payload=data[header_length:total_length],
        flags=flags,
    )


def parse_v3_data(value: object) -> V3Data:
    """Parse the fixed data shape shared by the observed write/read paths."""

    data, flags, header_length, total_length = _frame(value, DATA_HEADER_LENGTH)
    if data[4] != COMMAND_DATA:
        raise ValueError("not a Bolt v3 data frame")
    if header_length != DATA_HEADER_LENGTH:
        raise ValueError("invalid Bolt v3 data header length")
    return V3Data(
        session_id=struct.unpack_from(">I", data, 5)[0],
        connection_id=struct.unpack_from(">H", data, 9)[0],
        payload=data[header_length:total_length],
        flags=flags,
    )


def parse_v3_response(value: object) -> V3Response:
    """Parse the response shape consumed by the observed Android engine."""

    raw = _bytes(value, "frame")
    if len(raw) >= 5 and raw[4] == COMMAND_DATA:
        frame = parse_v3_data(raw)
        return V3Response(
            command=COMMAND_DATA,
            session_id=frame.session_id,
            connection_id=frame.connection_id,
            status=None,
            extensions=(),
            payload=frame.payload,
            flags=frame.flags,
        )

    data, flags, header_length, total_length = _frame(value, 12)
    command = data[4]
    session_id = struct.unpack_from(">I", data, 5)[0]
    connection_id = struct.unpack_from(">H", data, 9)[0]
    cursor = 11
    status: int | None = None
    if command in STATUS_COMMANDS:
        if cursor >= header_length:
            raise ValueError("Bolt v3 response status is truncated")
        status = data[cursor]
        cursor += 1
    if cursor >= header_length:
        raise ValueError("Bolt v3 response extension count is truncated")
    count = data[cursor]
    cursor += 1
    extensions = _parse_extensions(data, cursor, header_length, count)
    return V3Response(
        command=command,
        session_id=session_id,
        connection_id=connection_id,
        status=status,
        extensions=extensions,
        payload=data[header_length:total_length],
        flags=flags,
    )
