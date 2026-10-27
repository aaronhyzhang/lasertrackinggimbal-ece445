"""Minimal client for the PCB_CONTROL `G1` UDP protocol (one socket per axis).

Wire format, as implemented in firmware/Senior_Design_PCB_CONTROL/Core/Src/demo_network.c:

    G1 <session> <sequence> POSITION <0..360000 millidegrees>
    G1 <session> <sequence> TARGET <0|1>          (vision report; firmware with sys= in STATUS)
    G1 <session> <sequence> HEARTBEAT | STATUS | DISABLE | CONFIG
    G1 <session> <sequence> CALIBRATE | ZERO | HOME | FORGET   (calibration; HOME needs S1)

Accepted POSITION/TARGET/HEARTBEAT reply `OK G1 <session> <sequence>`; a refused one
replies `ERR G1 <session> <sequence>`. STATUS replies `S1 key=value ...`,
DISABLE replies `OK DISABLED`, malformed packets reply `ERR syntax`, and
ownership/sequence failures reply `ERR owner` / `ERR sequence`.

DISABLE on either port stops BOTH axes. Arming always needs the physical S1
button; no packet can arm the bridge. Standard library only.
"""
import secrets
import socket
import time

# position_control.h servo states
SERVO_OFF, SERVO_READY, SERVO_WAKE_RUN, SERVO_RUNNING, SERVO_FAULT, SERVO_HOMING = 0, 5, 6, 7, 8, 9
SERVO_CALIBRATING = (1, 2, 3, 4)
STATE_NAMES = {0: 'OFF', 1: 'WAKE_CAL', 2: 'ALIGN', 3: 'SWEEP', 4: 'RETURN', 5: 'READY',
               6: 'WAKE_RUN', 7: 'RUNNING', 8: 'FAULT', 9: 'HOMING'}
FAULT_NAMES = {0: 'OK', 1: 'DRIVER', 2: 'FEEDBACK', 3: 'ZERO_TIMEOUT', 4: 'CALIBRATION',
               5: 'CURRENT', 6: 'CURRENT_SUM', 7: 'TRAVEL', 8: 'FOLLOWING', 9: 'COMMAND_TIMEOUT',
               10: 'SUPPLY', 11: 'TEMPERATURE', 12: 'TIMING', 13: 'FOREGROUND', 14: 'INTERNAL'}
# system_fsm.h gimbal-level states, reported as STATUS sys=<n>
SYSTEM_NAMES = {0: 'INIT', 1: 'CALIBRATING', 2: 'IDLE', 3: 'ARMING', 4: 'TRACKING',
                5: 'SEARCHING', 6: 'FAULT'}
SYSTEM_REASONS = {0: 'NONE', 1: 'INIT_FAILED', 2: 'AXIS_FAULT', 3: 'BOARD_FAULT', 4: 'ESTOP',
                  5: 'SEARCH_TIMEOUT'}

DEFAULT_HOST = '192.168.10.2'
TILT_PORT = 5005  # axis 0, J3
PAN_PORT = 5006   # axis 1, J4


class G1Error(RuntimeError):
    """The board answered with an ERR reply."""

    def __init__(self, reply):
        super().__init__(reply)
        self.reply = reply


def parse_fields(text):
    """Parse `S1 a=1 b=x ...` / `C1 ...` into a dict, converting integers."""
    fields = {}
    for item in text.split()[1:]:
        key, _, value = item.partition('=')
        try:
            fields[key] = int(value)
        except ValueError:
            fields[key] = value
    return fields


class G1Client:
    """Request/response client for one axis port. Not thread-safe."""

    def __init__(self, host=DEFAULT_HOST, port=TILT_PORT, timeout=0.05):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect((host, port))
        self.timeout = timeout
        self.port = port
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0
        self.last_rtt = None

    def close(self):
        self.sock.close()

    def _drain(self):
        """Discard late replies from earlier timed-out requests (STATUS has no sequence)."""
        self.sock.setblocking(False)
        try:
            while True:
                self.sock.recv(2048)
        except (BlockingIOError, ConnectionResetError):
            pass
        finally:
            self.sock.setblocking(True)

    def send_raw(self, payload):
        """Send arbitrary bytes and return (reply text or None, round-trip seconds)."""
        self._drain()
        start = time.perf_counter()
        self.sock.send(payload)
        self.sock.settimeout(self.timeout)
        try:
            reply = self.sock.recv(2048).decode('ascii', 'replace')
        except (socket.timeout, ConnectionResetError):
            return None, None
        return reply, time.perf_counter() - start

    def request(self, operation, value=None):
        """Send one G1 command; return the reply text (or parsed fields for STATUS/CONFIG)."""
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        packet = f'G1 {self.session} {self.sequence} {operation}'
        if value is not None:
            packet += f' {value}'
        expected = f'OK G1 {self.session} {self.sequence}'
        self._drain()
        start = time.perf_counter()
        self.sock.send(packet.encode('ascii'))
        deadline = start + self.timeout
        while True:
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                raise TimeoutError(f'port {self.port}: no reply to {operation}')
            self.sock.settimeout(remaining)
            try:
                text = self.sock.recv(2048).decode('ascii', 'replace')
            except socket.timeout:
                continue
            except ConnectionResetError as exc:  # Windows ICMP port unreachable
                raise TimeoutError(f'port {self.port}: unreachable') from exc
            self.last_rtt = time.perf_counter() - start
            if text.startswith('ERR'):
                raise G1Error(text)
            if operation == 'STATUS' and text.startswith('S1 '):
                return parse_fields(text)
            if operation == 'CONFIG' and text.startswith('C1 '):
                return parse_fields(text)
            if operation == 'DISABLE' and text == 'OK DISABLED':
                return text
            if text == expected:
                return text
            # Anything else is a stale reply; keep waiting for ours.

    def status(self):
        return self.request('STATUS')

    def position(self, degrees):
        return self.request('POSITION', round(degrees * 1000))

    def target(self, detected):
        return self.request('TARGET', 1 if detected else 0)

    def calibrate(self):
        return self.request('CALIBRATE')

    def zero(self):
        return self.request('ZERO')

    def home(self):
        return self.request('HOME')

    def forget(self):
        return self.request('FORGET')

    def heartbeat(self):
        return self.request('HEARTBEAT')

    def disable(self):
        return self.request('DISABLE')
