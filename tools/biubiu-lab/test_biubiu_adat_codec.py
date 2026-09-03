#!/usr/bin/env python3

import base64
import json
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding as rsa_padding
from cryptography.hazmat.primitives.asymmetric import rsa

import biubiu_adat_codec as adat


class AdatCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.private_key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        cls.public_key = cls.private_key.public_key()

    def test_request_uses_independent_wrapped_key_and_iv(self) -> None:
        keys = adat.SessionKeys(b"0123456789abcdef", b"fedcba9876543210")
        envelope, returned_keys = adat.encrypt_request(
            {"id": "offline-test", "unicode": "\u6d4b\u8bd5"},
            self.public_key,
            7,
            keys,
        )

        self.assertEqual(returned_keys, keys)
        self.assertEqual(list(envelope), ["k", "v", "d", "i"])
        self.assertEqual(envelope["v"], 7)
        self.assertNotEqual(envelope["k"], envelope["i"])
        self.assertEqual(
            self.private_key.decrypt(
                base64.b64decode(envelope["k"]), rsa_padding.PKCS1v15()
            ),
            keys.key,
        )
        self.assertEqual(
            self.private_key.decrypt(
                base64.b64decode(envelope["i"]), rsa_padding.PKCS1v15()
            ),
            keys.iv,
        )

        response = {"c": 0, "d": envelope["d"]}
        self.assertEqual(
            adat.decrypt_response(response, keys),
            {"id": "offline-test", "unicode": "\u6d4b\u8bd5"},
        )

    def test_rotation_signal_is_not_treated_as_ciphertext(self) -> None:
        with self.assertRaises(adat.KeyRotationRequired):
            adat.decrypt_response(
                {"c": 2, "d": "ignored"},
                adat.SessionKeys(b"0" * 16, b"1" * 16),
            )

    def test_rotated_key_parser_validates_version_and_key(self) -> None:
        encoded = base64.b64encode(
            self.public_key.public_bytes(
                serialization.Encoding.DER,
                serialization.PublicFormat.SubjectPublicKeyInfo,
            )
        ).decode()
        version, key = adat.parse_rotated_public_key("9|" + encoded)
        self.assertEqual(version, 9)
        self.assertEqual(key.public_numbers(), self.public_key.public_numbers())

        for invalid in ("", "0|" + encoded, "x|" + encoded, "1|not-base64"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                adat.parse_rotated_public_key(invalid)

    def test_response_rejects_invalid_data(self) -> None:
        keys = adat.SessionKeys(b"0" * 16, b"1" * 16)
        for response in ({"c": 0}, {"c": 0, "d": "***"}):
            with self.subTest(response=json.dumps(response)), self.assertRaises(
                ValueError
            ):
                adat.decrypt_response(response, keys)


if __name__ == "__main__":
    unittest.main()
