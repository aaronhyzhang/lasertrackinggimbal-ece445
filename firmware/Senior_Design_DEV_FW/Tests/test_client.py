import importlib.util
import pathlib
import socket
import threading
import unittest

spec = importlib.util.spec_from_file_location("udp_client", pathlib.Path(__file__).resolve().parents[1] / "Tools/udp_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)


class ClientTests(unittest.TestCase):
    def test_degree_validation(self):
        self.assertEqual(client_module.millidegrees("360"), 360000)
        self.assertEqual(client_module.millidegrees("90.125"), 90125)
        for value in ["NaN", "Infinity", "-1", "360.001", "abc"]:
            with self.assertRaises(ValueError):
                client_module.millidegrees(value)

    def test_wire_format_and_acknowledgement(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2)
        received = []

        def worker():
            try:
                for _ in range(3):
                    data, address = server.recvfrom(512)
                    parts = data.decode("ascii").split(" ")
                    received.append(parts)
                    response = "OK DISABLED" if parts[3] == "DISABLE" else f"OK G1 {parts[1]} {parts[2]}"
                    server.sendto(response.encode("ascii"), address)
            finally:
                server.close()

        thread = threading.Thread(target=worker)
        thread.start()
        client = client_module.Client("127.0.0.1", server.getsockname()[1], 0)
        try:
            client.request("POSITION", 90125)
            client.request("HEARTBEAT")
            self.assertEqual(client.disable(), "OK DISABLED")
        finally:
            client.close()
            thread.join(3)
        self.assertEqual([p[3] for p in received], ["POSITION", "HEARTBEAT", "DISABLE"])
        self.assertEqual(received[0][4], "90125")
        self.assertEqual([int(p[2]) for p in received], [1, 2, 3])
        self.assertEqual(len({p[1] for p in received}), 1)
