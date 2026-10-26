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


if __name__ == "__main__":
    unittest.main()