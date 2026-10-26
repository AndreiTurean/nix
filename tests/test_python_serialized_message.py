import unittest

from nixipc import decode_message, encode_message


class SerializedMessageInteropTest(unittest.TestCase):
    def test_canonical_golden_frame_matches_cpp_format(self):
        frame = encode_message("s/v1", "raw", b"\x01\x02")
        expected = bytes.fromhex(
            "4e49584d000100040003000000000002"
            "732f76317261770102"
        )
        self.assertEqual(frame[: len(expected)], expected)
        self.assertEqual(frame[len(expected) :], bytes(len(frame) - len(expected)))
        self.assertEqual(decode_message(frame), ("s/v1", "raw", b"\x01\x02"))

    def test_decoder_rejects_invalid_frames(self):
        frame = bytearray(encode_message("s/v1", "raw", b"\x01"))
        frame[0] = ord("X")
        with self.assertRaises(ValueError):
            decode_message(bytes(frame))

    def test_maximum_63_kib_payload_round_trips(self):
        payload = bytes(range(256)) * 252
        self.assertEqual(len(payload), 63 * 1024)
        frame = encode_message("schema/v1", "application/octet-stream", payload)
        self.assertEqual(decode_message(frame), ("schema/v1", "application/octet-stream", payload))

    def test_payload_over_63_kib_is_rejected(self):
        payload = bytes(63 * 1024 + 1)
        with self.assertRaises(RuntimeError):
            encode_message("schema/v1", "application/octet-stream", payload)


if __name__ == "__main__":
    unittest.main()