#!/usr/bin/env python3

import struct
import unittest

import biubiu_bolt_model as bolt


class BoltModelTests(unittest.TestCase):
    def test_v2_bind_request_matches_verified_windows_layout(self) -> None:
        channel_token = b"0123456789abcdef0123456789abcdef"
        channel_st = b"0123456789abcdef"
        frame = bolt.encode_v2_bind(
            channel_token,
            0x01020304,
            channel_st,
            0x12345678,
        )
        expected_header = bytes.fromhex(
            "00 15 5e00 00 00000000 0000 00000000 0000 00000000"
        )
        expected_payload = (
            bytes.fromhex("01 78563412")
            + channel_token
            + bytes.fromhex("04030201")
            + channel_st
        )

        self.assertEqual(len(expected_header), bolt.BIND_HEADER_LENGTH)
        self.assertEqual(
            frame,
            expected_header
            + expected_payload.ljust(bolt.BIND_PAYLOAD_LENGTH, b"\x00"),
        )

    def test_v2_bind_ept_extension_and_response_are_verified(self) -> None:
        request = bolt.encode_v2_bind(b"0123456789abcdef0123456789abcdef", 7, b"ticket", 9, tcp=True)
        self.assertEqual(request[0], bolt.BIND_TCP_REQUEST_TYPE)
        self.assertEqual(
            request[1], bolt.BIND_HEADER_LENGTH + len(bolt.BIND_EPT_REQUEST)
        )
        self.assertEqual(
            request[bolt.BIND_HEADER_LENGTH : request[1]], bolt.BIND_EPT_REQUEST
        )
        self.assertEqual(struct.unpack_from("<H", request, 2)[0], len(request))

        extension = b"mode=1;ept_key=K"
        response = bytearray(bolt.BIND_HEADER_LENGTH + len(extension) + 13)
        response[0] = bolt.BIND_TCP_REQUEST_TYPE
        response[1] = bolt.BIND_HEADER_LENGTH + len(extension)
        struct.pack_into("<H", response, 2, len(response))
        response[response[1]] = bolt.BIND_RESPONSE_TYPE
        struct.pack_into("<I", response, response[1] + 9, 1)
        response[bolt.BIND_HEADER_LENGTH : response[1]] = extension

        parsed = bolt.parse_v2_bind_response(bytes(response))
        self.assertTrue(parsed.successful)
        self.assertEqual(parsed.ept_key, ord("K"))

    def test_invalid_v2_bind_values_and_responses_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            bolt.encode_v2_bind(b"", 1, b"ticket", 1)
        with self.assertRaises(ValueError):
            bolt.encode_v2_bind(b"route", 0, b"ticket", 1)
        with self.assertRaises(ValueError):
            bolt.encode_v2_bind(b"r" * 33, 1, b"t" * 32, 1)

        response = bytearray(bolt.BIND_HEADER_LENGTH + 13)
        response[0] = bolt.BIND_REQUEST_TYPE
        response[1] = bolt.BIND_HEADER_LENGTH
        struct.pack_into("<H", response, 2, len(response))
        response[bolt.BIND_HEADER_LENGTH] = bolt.BIND_RESPONSE_TYPE
        struct.pack_into("<I", response, bolt.BIND_HEADER_LENGTH + 9, 1)
        self.assertTrue(bolt.parse_v2_bind_response(bytes(response)).successful)

        invalid_type = bytearray(response)
        invalid_type[0] = bolt.BIND_RESPONSE_TYPE
        with self.assertRaises(ValueError):
            bolt.parse_v2_bind_response(bytes(invalid_type))

        failed = bytearray(response)
        struct.pack_into("<I", failed, bolt.BIND_HEADER_LENGTH + 9, 2)
        with self.assertRaises(ValueError):
            bolt.parse_v2_bind_response(bytes(failed))

        truncated_ept = (response[:bolt.BIND_HEADER_LENGTH] + bolt.BIND_EPT_KEY_PREFIX
                         + response[bolt.BIND_HEADER_LENGTH:])
        truncated_ept[1] = bolt.BIND_HEADER_LENGTH + len(bolt.BIND_EPT_KEY_PREFIX)
        struct.pack_into("<H", truncated_ept, 2, len(truncated_ept))
        with self.assertRaises(ValueError):
            bolt.parse_v2_bind_response(bytes(truncated_ept))

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
                "03 1c 1c 00 22 04030201 03 "
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
        self.assertEqual(struct.unpack_from("<H", frame, 2)[0], len(frame))
        self.assertEqual(parsed.command, bolt.COMMAND_ASSOCIATE_REQUEST)
        self.assertEqual(parsed.payload, b"payload")

    def test_success_response_is_parsed(self) -> None:
        extension = bytes.fromhex("01 06 cb007108 6987")
        header_length = 13 + len(extension)
        frame = (
            struct.pack(
                "<BBHBIHBB",
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
            bytes.fromhex("03 0b 0f00 11 04030201 3412 deadbeef"),
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
