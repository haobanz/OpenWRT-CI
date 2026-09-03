#!/usr/bin/env python3
"""Offline reference codec for the acceleration API's ADAT envelope."""

from __future__ import annotations

import base64
from dataclasses import dataclass
import json
import os
from typing import Any

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding as rsa_padding
from cryptography.hazmat.primitives.asymmetric.rsa import RSAPublicKey
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.padding import PKCS7


AES_SIZE = 16


class KeyRotationRequired(RuntimeError):
    """The server rejected the current RSA key version."""


@dataclass(frozen=True)
class SessionKeys:
    key: bytes
    iv: bytes

    def __post_init__(self) -> None:
        if len(self.key) != AES_SIZE or len(self.iv) != AES_SIZE:
            raise ValueError("ADAT requires a 16-byte AES key and IV")

    @classmethod
    def random(cls) -> "SessionKeys":
        return cls(os.urandom(AES_SIZE), os.urandom(AES_SIZE))


def load_public_key(der_b64: str) -> RSAPublicKey:
    try:
        der = base64.b64decode(der_b64, validate=True)
        key = serialization.load_der_public_key(der)
    except (TypeError, ValueError) as exc:
        raise ValueError("invalid base64 X.509 public key") from exc
    if not isinstance(key, RSAPublicKey) or key.key_size < 1024:
        raise ValueError("ADAT requires an RSA public key of at least 1024 bits")
    return key


def _json_bytes(value: Any) -> bytes:
    if isinstance(value, bytes):
        return value
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode()


def _aes_encrypt(plaintext: bytes, keys: SessionKeys) -> bytes:
    padder = PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(algorithms.AES(keys.key), modes.CBC(keys.iv)).encryptor()
    return encryptor.update(padded) + encryptor.finalize()


def _aes_decrypt(ciphertext: bytes, keys: SessionKeys) -> bytes:
    if not ciphertext or len(ciphertext) % AES_SIZE:
        raise ValueError("invalid ADAT ciphertext length")
    decryptor = Cipher(algorithms.AES(keys.key), modes.CBC(keys.iv)).decryptor()
    padded = decryptor.update(ciphertext) + decryptor.finalize()
    unpadder = PKCS7(128).unpadder()
    try:
        return unpadder.update(padded) + unpadder.finalize()
    except ValueError as exc:
        raise ValueError("invalid ADAT response padding") from exc


def encrypt_request(
    payload: Any,
    public_key: RSAPublicKey,
    version: int,
    keys: SessionKeys | None = None,
) -> tuple[dict[str, Any], SessionKeys]:
    if version < 1:
        raise ValueError("public key version must be positive")
    if not isinstance(public_key, RSAPublicKey) or public_key.key_size < 1024:
        raise ValueError("ADAT requires an RSA public key of at least 1024 bits")
    keys = keys or SessionKeys.random()
    ciphertext = _aes_encrypt(_json_bytes(payload), keys)
    wrapped_key = public_key.encrypt(keys.key, rsa_padding.PKCS1v15())
    wrapped_iv = public_key.encrypt(keys.iv, rsa_padding.PKCS1v15())
    return {
        "k": base64.b64encode(wrapped_key).decode(),
        "v": version,
        "d": base64.b64encode(ciphertext).decode(),
        "i": base64.b64encode(wrapped_iv).decode(),
    }, keys


def decrypt_response(response: dict[str, Any], keys: SessionKeys) -> Any:
    if response.get("c") == 2:
        raise KeyRotationRequired("server requested an ADAT public-key refresh")
    encoded = response.get("d")
    if not isinstance(encoded, str) or not encoded:
        raise ValueError("ADAT response has no encrypted data")
    try:
        ciphertext = base64.b64decode(encoded, validate=True)
    except (TypeError, ValueError) as exc:
        raise ValueError("ADAT response data is not valid base64") from exc
    return json.loads(_aes_decrypt(ciphertext, keys))


def parse_rotated_public_key(value: str) -> tuple[int, RSAPublicKey]:
    try:
        version_text, encoded_key = value.split("|", 1)
        version = int(version_text)
    except (AttributeError, TypeError, ValueError) as exc:
        raise ValueError("invalid ADAT securityKey value") from exc
    if version < 1:
        raise ValueError("public key version must be positive")
    return version, load_public_key(encoded_key)
