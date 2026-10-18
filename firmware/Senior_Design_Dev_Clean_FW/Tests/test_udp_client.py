"""Client message compatibility tests; no network sockets or board required."""
import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / "Tools/udp_client.py"
spec = importlib.util.spec_from_file_location("gimbal_udp_client", path)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class ClientTests(unittest.TestCase):
    def test_vision_format(self):
        self.assertEqual(client.vision_message(42, 10, True, 5, -2),
                         b"V1 VISION 42 10 1 5000 -2000\n")
        self.assertEqual(client.vision_message(0, 0xffffffff, False, 0, 0),
                         b"V1 VISION 0 4294967295 0 0 0\n")

    def test_invalid_observation(self):
        for angle in (float("nan"), float("inf"), 1e308, 2147483.648):
            with self.subTest(angle=angle), self.assertRaises(ValueError):
                client.vision_message(42, 1, True, angle, 0)
        for session, sequence in ((-1, 0), (0x100000000, 0), (1, -1)):
            with self.assertRaises(ValueError):
                client.vision_message(session, sequence, True, 0, 0)

    def test_status(self):
        status = client.parse_status(b"V1 STATUS 42 10 0 0 0 0 2\n")
        self.assertEqual(status["session"], 42)
        self.assertEqual(status["accepted_sequence"], 10)
        self.assertEqual(status["fault_flags"], 2)
        self.assertEqual(status["angles_valid"], 0)
        self.assertEqual(status["armed"], 0)

    def test_invalid_status(self):
        for message in (b"ERR syntax\n", b"V2 STATUS 42 10 0 0 0 0 0",
                        b"V1 STATUS 42 10 2 0 0 0 0",
                        b"V1 STATUS -1 10 0 0 0 0 0",
                        b"V1 STATUS 42 10 0 2147483648 0 0 0",
                        b"V1 STATUS 42 10 0 0 0 0 0 extra"):
            with self.subTest(message=message), self.assertRaises(ValueError):
                client.parse_status(message)


if __name__ == "__main__":
    unittest.main()
