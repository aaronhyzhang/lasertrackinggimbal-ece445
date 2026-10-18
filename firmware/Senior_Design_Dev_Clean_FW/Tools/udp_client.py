"""Send HELLO, STATUS or vision observations to the H753; standard library only."""

import argparse
import math
import secrets
import socket
import time


def vision_message(session, sequence, detected, pan_deg, tilt_deg):
    if not 0 <= session <= 0xFFFFFFFF or not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError("Session and sequence must fit uint32_t")
    if not all(math.isfinite(value) for value in (pan_deg, tilt_deg)):
        raise ValueError("Angles must be finite")
    scaled = (pan_deg * 1000, tilt_deg * 1000)
    if not all(math.isfinite(value) for value in scaled):
        raise ValueError("Angles are too large")
    pan, tilt = (round(value) for value in scaled)
    if not all(-0x80000000 <= value <= 0x7FFFFFFF for value in (pan, tilt)):
        raise ValueError("Angles must fit signed 32-bit millidegrees")
    return f"V1 VISION {session} {sequence} {int(bool(detected))} {pan} {tilt}\n".encode("ascii")


def parse_status(packet):
    fields = packet.decode("ascii").split()
    if len(fields) != 9 or fields[:2] != ["V1", "STATUS"]:
        raise ValueError("Unexpected status reply")
    values = [int(field) for field in fields[2:]]
    for index in (0, 1, 6):
        if not 0 <= values[index] <= 0xFFFFFFFF:
            raise ValueError("Invalid unsigned status field")
    for index in (2, 5):
        if values[index] not in (0, 1): raise ValueError("Invalid boolean status field")
    for index in (3, 4):
        if not -0x80000000 <= values[index] <= 0x7FFFFFFF:
            raise ValueError("Invalid angle status field")
    return dict(zip(("session", "accepted_sequence", "angles_valid", "pan_mdeg",
                     "tilt_mdeg", "armed", "fault_flags"), values))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["hello", "status", "vision", "reset"], nargs="?", default="hello")
    parser.add_argument("--host", default="192.168.10.2")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--session", type=int, default=None)
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--pan", type=float, default=5.0, help="Camera error in degrees; positive right")
    parser.add_argument("--tilt", type=float, default=-2.0, help="Camera error in degrees; positive up")
    parser.add_argument("--lost", action="store_true", help="Report no detected target")
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--interval", type=float, default=0.1)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--reset-session", action="store_true", help="Explicitly clear bench ownership first")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535 or args.count < 1:
        parser.error("Port must be 1..65535 and count must be positive")
    if not math.isfinite(args.interval) or args.interval < 0 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("Interval must be finite/nonnegative; timeout must be finite/positive")
    session = secrets.randbits(32) if args.session is None else args.session
    try:
        # Validate all observation arguments before opening a socket.
        vision_message(session, args.sequence, not args.lost, args.pan, args.tilt)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.settimeout(args.timeout)
            connection.connect((args.host, args.port))

            def exchange(message):
                connection.send(message)
                reply = connection.recv(2048)
                print("TX:", message.decode("ascii").rstrip())
                print("RX:", reply.decode("ascii").rstrip())
                if reply.startswith(b"ERR "):
                    raise ValueError("Board rejected request; use --reset-session when changing client sessions")
                return reply

            if args.reset_session:
                if exchange(b"RESET_SESSION\n") != b"OK RESET_SESSION\n":
                    raise ValueError("Unexpected reset acknowledgement")
            for index in range(args.count):
                sequence = (args.sequence + index) & 0xFFFFFFFF
                if args.mode == "vision":
                    request = vision_message(session, sequence, not args.lost, args.pan, args.tilt)
                else:
                    request = {"hello": b"HELLO\n", "status": b"STATUS\n",
                               "reset": b"RESET_SESSION\n"}[args.mode]
                response = exchange(request)
                if args.mode == "hello" and response != request:
                    raise ValueError("Echo does not match request")
                if args.mode in ("vision", "status"):
                    status = parse_status(response)
                    if args.mode == "vision" and (status["session"] != session or
                                                   status["accepted_sequence"] != sequence):
                        raise ValueError("Acknowledgement session/sequence mismatch")
                if args.mode == "reset" and response != b"OK RESET_SESSION\n":
                    raise ValueError("Unexpected reset acknowledgement")
                if index + 1 < args.count: time.sleep(args.interval)
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, f"Communication failed: {error}\n")


if __name__ == "__main__":
    main()
