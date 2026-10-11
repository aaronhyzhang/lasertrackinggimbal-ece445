"""Bench UDP client. MCU must be locally calibrated, zeroed and armed via VCP.
Start --angle 0 --hold before issuing ARM, then type degree targets on this console.
Ctrl+C, quit, or communication failure sends DISABLE; MCU also enforces a 500 ms lease.
"""
import argparse
import queue
import secrets
import socket
import threading
import time


def millidegrees(text):
    from decimal import Decimal, InvalidOperation
    try:
        degrees = Decimal(str(text))
        if not degrees.is_finite() or not 0 <= degrees <= 360:
            raise ValueError("angle must be between 0 and 360 degrees")
        return int(degrees * 1000)
    except InvalidOperation as exc:
        raise ValueError("angle must be numeric") from exc


class Client:
    def __init__(self, ip, port, local_port=40000, timeout=0.2):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("", local_port))
        self.socket.connect((ip, port))
        self.socket.settimeout(timeout)
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0

    def request(self, operation, value=None):
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        command = f"G1 {self.session} {self.sequence} {operation}"
        if value is not None:
            command += f" {value}"
        self.socket.send(command.encode("ascii"))
        deadline = time.monotonic() + self.socket.gettimeout()
        while time.monotonic() < deadline:
            response = self.socket.recv(1024).decode("ascii")
            if operation == "STATUS" and response.startswith("state="):
                return response.strip()
            if operation == "DISABLE" and response == "OK DISABLED":
                return response
            if response == f"OK G1 {self.session} {self.sequence}":
                return response
            if response.startswith("ERR"):
                raise RuntimeError(response)
        raise TimeoutError("matching acknowledgement not received")

    def disable(self):
        for _ in range(3):
            try:
                return self.request("DISABLE")
            except (OSError, RuntimeError, TimeoutError):
                pass
        return "DISABLE sent; no acknowledgement (MCU command lease will expire)"

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default="192.168.10.2")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--local-port", type=int, default=40000)
    parser.add_argument("--angle", help="initial angle in degrees")
    parser.add_argument("--hold", action="store_true", help="maintain heartbeat and accept typed degree targets")
    parser.add_argument("--disable", action="store_true")
    args = parser.parse_args()
    if args.hold and args.angle is None:
        parser.error("--hold requires an initial --angle")
    try:
        initial = millidegrees(args.angle) if args.angle is not None else None
    except ValueError as exc:
        parser.error(str(exc))
    client = Client(args.ip, args.port, args.local_port)
    controlling = initial is not None
    try:
        if args.disable:
            print(client.disable())
        elif initial is None:
            print(client.request("STATUS"))
        else:
            print(client.request("POSITION", initial))
            if not args.hold:
                print("One target sent; command lease expires after 500 ms. Use --hold for motion.")
                return
            print("Session active. Issue ARM on ST-LINK VCP. Enter 0..360 degrees, or quit.")
            commands = queue.Queue()

            def input_worker():
                while True:
                    try:
                        line = input().strip()
                    except EOFError:
                        line = "quit"
                    commands.put(line)
                    if line.lower() == "quit":
                        return

            threading.Thread(target=input_worker, daemon=True).start()
            next_status = time.monotonic()
            while True:
                try:
                    line = commands.get_nowait()
                except queue.Empty:
                    line = None
                if line is not None:
                    if line.lower() == "quit":
                        break
                    try:
                        value = millidegrees(line)
                    except ValueError as exc:
                        print(exc)
                    else:
                        print(client.request("POSITION", value))
                client.request("HEARTBEAT")
                if time.monotonic() >= next_status:
                    print(client.request("STATUS"))
                    next_status = time.monotonic() + 1
                time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError, TimeoutError) as exc:
        print(f"Communication stopped: {exc}")
    finally:
        if controlling:
            print(client.disable())
        client.close()


if __name__ == "__main__":
    main()
