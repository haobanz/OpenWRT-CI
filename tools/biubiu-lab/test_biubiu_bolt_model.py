#!/usr/bin/env python3

import struct
import unittest

import biubiu_bolt_model as bolt


class BoltModelTests(unittest.TestCase):
    def test_connect_request_matches_verified_layout(self) -> None:
        frame = bolt.encode_v3_request(
            bolt.COMMAND_CONNECT_REQUEST,
            0x01020304,
            (
                bolt.Extension.endpoint(1, bolt.Endpoint("192.0.2.1", 443)),
                bolt.Extension(6, b"alpha"),
                bolt.Extension(5, b"\x01"),
            ),
        )

        self.assertEqual(
            frame,
            bytes.fromhex(
                "03 1c 00 1c 22 01020304 03 "
                "01 06 c0000201 01bb "
                "06 05 616c706861 "
                "05 01 01"
            ),
        )

        parsed = bolt.parse_v3_request(frame)
        self.assertEqual(parsed.command, bolt.COMMAND_CONNECT_REQUEST)
        self.assertEqual(parsed.session_id, 0x01020304)
        self.assertEqual(
            parsed.extensions[0].as_endpoint(), bolt.Endpoint("192.0.2.1", 443)
        )
        self.assertEqual(parsed.payload, b"")

    def test_associate_request_preserves_separate_payload(self) -> None:
        frame = bolt.encode_v3_request(
            bolt.COMMAND_ASSOCIATE_REQUEST,
            7,
            (bolt.Extension(9, b"opaque"),),
            payload=b"payload",
        )
        parsed = bolt.parse_v3_request(frame)

        self.assertEqual(frame[1], 18)
        self.assertEqual(struct.unpack_from(">H", frame, 2)[0], len(frame))
        self.assertEqual(parsed.command, bolt.COMMAND_ASSOCIATE_REQUEST)
        self.assertEqual(parsed.payload, b"payload")

    def test_success_response_is_parsed(self) -> None:
        extension = bytes.fromhex("01 06 cb007108 6987")
        header_length = 13 + len(extension)
        frame = (
            struct.pack(
                ">BBHB I H BB",
                bolt.PROTOCOL_VERSION,
                header_length,
                header_length + 3,
                bolt.COMMAND_ASSOCIATE_RESPONSE,
                0x01020304,
                0x1234,
                bolt.STATUS_SUCCESS,
                1,
            )
            + extension
            + b"udp"
        )

        parsed = bolt.parse_v3_response(frame)
        self.assertTrue(parsed.successful)
        self.assertTrue(parsed.successful_for(bolt.COMMAND_ASSOCIATE_REQUEST))
        self.assertFalse(parsed.successful_for(bolt.COMMAND_CONNECT_REQUEST))
        self.assertEqual(parsed.connection_id, 0x1234)
        self.assertEqual(
            parsed.extensions[0].as_endpoint(), bolt.Endpoint("203.0.113.8", 27015)
        )
        self.assertEqual(parsed.payload, b"udp")

        zero_connection = bytearray(frame)
        zero_connection[9:11] = b"\x00\x00"
        self.assertFalse(bolt.parse_v3_response(bytes(zero_connection)).successful)
        self.assertFalse(
            bolt.parse_v3_response(bytes(zero_connection)).successful_for(
                bolt.COMMAND_ASSOCIATE_REQUEST
            )
        )

        with self.assertRaises(ValueError):
            parsed.successful_for(bolt.COMMAND_DATA)

    def test_data_frame_matches_verified_layout(self) -> None:
        marker = b"\xde\xad\xbe\xef"
        frame = bolt.encode_v3_data(
            session_id=0x01020304,
            connection_id=0x1234,
            payload=marker,
        )

        self.assertEqual(
            frame,
            bytes.fromhex("03 0b 000f 11 01020304 1234 deadbeef"),
        )

        data = bolt.parse_v3_data(frame)
        self.assertEqual(data.session_id, 0x01020304)
        self.assertEqual(data.connection_id, 0x1234)
        self.assertEqual(data.payload, marker)
        self.assertNotIn(marker.hex(), repr(data))

        parsed = bolt.parse_v3_response(frame)
        self.assertIsNone(parsed.status)
        self.assertEqual(parsed.extensions, ())
        self.assertEqual(parsed.payload, marker)
        self.assertFalse(parsed.successful)

    def test_opaque_values_are_not_rendered(self) -> None:
        marker = b"private-channel-parameter"
        extension = bolt.Extension(10, marker)
        request = bolt.parse_v3_request(
            bolt.encode_v3_request(
                bolt.COMMAND_CONNECT_REQUEST,
                1,
                (extension,),
                payload=marker,
            )
        )
        self.assertNotIn(marker.decode(), repr(extension))
        self.assertNotIn(marker.decode(), repr(request))

    def test_invalid_frames_are_rejected(self) -> None:
        valid = bolt.encode_v3_request(bolt.COMMAND_CONNECT_REQUEST, 1)
        invalid_frames = (
            valid[:4],
            bytes((2,)) + valid[1:],
            valid[:1] + b"\x09" + valid[2:],
            valid[:2] + b"\x00\x0b" + valid[4:],
            valid[:-1],
            valid[:9] + b"\x01",
        )
        for frame in invalid_frames:
            with self.subTest(frame=frame.hex()), self.assertRaises(ValueError):
                bolt.parse_v3_request(frame)

    def test_invalid_values_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            bolt.encode_v3_request(0x99, 1)
        with self.assertRaises(ValueError):
            bolt.Endpoint("2001:db8::1", 443)
        with self.assertRaises(ValueError):
            bolt.Endpoint("192.0.2.1", 0)
        with self.assertRaises(ValueError):
            bolt.Extension.endpoint(6, bolt.Endpoint("192.0.2.1", 443))
        with self.assertRaises(ValueError):
            bolt.Extension(1, b"x" * 256)
        with self.assertRaises(ValueError):
            bolt.encode_v3_data(1, 1, b"x" * (0x10000 - 10))
        with self.assertRaises(ValueError):
            bolt.parse_v3_data(
                bolt.encode_v3_request(bolt.COMMAND_CONNECT_REQUEST, 1)
            )

        data = bytearray(bolt.encode_v3_data(1, 2, b"payload"))
        data[1] = 12
        with self.assertRaises(ValueError):
            bolt.parse_v3_data(bytes(data))


if __name__ == "__main__":
    unittest.main()
