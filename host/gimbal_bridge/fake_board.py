"""Localhost stand-in for the dual-axis PCB_CONTROL firmware, for testing host tools.

Mirrors the rules in demo_network.c / demo_dual.inc / demo_protocol.c that host code
depends on: strict G1 parsing, per-port owner (IP, port, session) and sequence order,
500 ms lease, POSITION accepted only when READY/WAKE_RUN/RUNNING and zeroed, S1 arms
all READY axes with a queued position, DISABLE / either fault / lease loss stops BOTH.
With legacy=False (default) it also accepts TARGET <0|1> and reports a simplified
system state as sys=<n> (system_fsm.h numbering); legacy=True mimics older firmware.
Motor dynamics are a simple rate-limited move, not a motor model.

    py -3 -m gimbal_bridge.fake_board          # serves 127.0.0.1:5005/5006, press Enter for S1
"""
import socket
import threading
import time

OFF, READY, WAKE_RUN, RUNNING, FAULT = 0, 5, 6, 7, 8
COMMAND_TIMEOUT = 9
SYS_IDLE, SYS_ARMING, SYS_TRACKING, SYS_SEARCHING, SYS_FAULT = 2, 3, 4, 5, 6
TARGET_STALE_S = 0.3
LEASE_S = 0.5
MAX_MDEG = (360000, 337000)  # tilt, pan accept limits (demo_dual.inc app_position)


def parse(data, legacy=False):
    """Python port of protocol_parse(); returns (session, sequence, op, value) or None."""
    if not 0 < len(data) < 192 or any(b < 32 or b > 126 for b in data):
        return None
    tokens = data.decode('ascii').split(' ')
    if '' in tokens or len(tokens) < 4 or tokens[0] != 'G1':
        return None

    def u32(text):
        return int(text) if text.isdigit() and int(text) <= 0xFFFFFFFF else None

    session, sequence = u32(tokens[1]), u32(tokens[2])
    if not session or sequence is None:
        return None
    op = tokens[3]
    if op == 'POSITION':
        value = u32(tokens[4]) if len(tokens) == 5 else None
        return (session, sequence, op, value) if value is not None and value <= 360000 else None
    if op == 'TARGET' and not legacy:
        value = u32(tokens[4]) if len(tokens) == 5 else None
        return (session, sequence, op, value) if value in (0, 1) else None
    if len(tokens) == 4 and op in ('HEARTBEAT', 'STATUS', 'DISABLE', 'CALIBRATE', 'ZERO', 'CONFIG', 'TESTINFO'):
        return session, sequence, op, None
    return None


class Axis:
    def __init__(self, index, position_deg):
        self.index = index
        self.state, self.fault = READY, 0
        self.position = self.target = position_deg
        self.valid = False
        self.command_s = 0.0
        self.owner = None  # (ip, port, session)
        self.owner_seq = 0
        self.owner_s = 0.0


class FakeBoard:
    def __init__(self, host='127.0.0.1', ports=(0, 0), positions=(90.0, 150.0), speed_dps=300.0, latency_s=0.0,
                 legacy=False):
        self.axes = [Axis(0, positions[0]), Axis(1, positions[1])]
        self.legacy = legacy
        self.target_detected, self.target_s, self.targets = False, None, 0
        self.speed = speed_dps
        self.latency = latency_s
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.socks = []
        for port in ports:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.bind((host, port))
            s.settimeout(0.02)
            self.socks.append(s)
        self.ports = tuple(s.getsockname()[1] for s in self.socks)
        self.rejected = 0
        self.threads = [threading.Thread(target=self._serve, args=(i,), daemon=True) for i in (0, 1)]
        self.threads.append(threading.Thread(target=self._physics, daemon=True))

    def start(self):
        for t in self.threads:
            t.start()
        return self

    def close(self):
        self.stop_event.set()
        for t in self.threads:
            t.join(timeout=1)
        for s in self.socks:
            s.close()

    # ---- physical buttons --------------------------------------------------
    def press_s1(self):
        with self.lock:
            for a in self.axes:
                if a.valid and a.state == READY:
                    a.state = WAKE_RUN

    def press_s2(self):
        with self.lock:
            self._stop_all()

    def _stop_all(self, fault_axis=None, fault=0):
        for a in self.axes:
            a.valid = False
            a.owner = None
            if a.state != FAULT:
                a.state = READY
        if fault_axis is not None:
            self.axes[fault_axis].state, self.axes[fault_axis].fault = FAULT, fault

    # ---- simulation --------------------------------------------------------
    def _physics(self):
        last = time.monotonic()
        while not self.stop_event.is_set():
            time.sleep(0.002)
            now = time.monotonic()
            dt, last = now - last, now
            with self.lock:
                for a in self.axes:
                    if a.state == WAKE_RUN:
                        a.state = RUNNING
                    if a.state == RUNNING:
                        if now - a.command_s > LEASE_S:
                            self._stop_all(a.index, COMMAND_TIMEOUT)
                            break
                        step = self.speed * dt
                        a.position += max(-step, min(step, a.target - a.position))

    def system_state(self):
        states = [a.state for a in self.axes]
        if FAULT in states:
            return SYS_FAULT
        if WAKE_RUN in states:
            return SYS_ARMING
        if RUNNING in states:
            fresh = self.target_s is not None and time.monotonic() - self.target_s <= TARGET_STALE_S
            return SYS_TRACKING if fresh and self.target_detected else SYS_SEARCHING
        return SYS_IDLE

    def _status(self, a):
        settled = int(a.state == RUNNING and abs(a.position - a.target) < 0.5)
        extra = '' if self.legacy else f' sys={self.system_state()} sys_reason=0'
        return (f'S1 axis={a.index} source=1 enabled=1 state={a.state} fault={a.fault} calibrated=1 zero=1 '
                f'position_mdeg={round(a.position * 1000)} target_mdeg={round(a.target * 1000)} rpm_milli=0 '
                f'iq_ma=0 current_age_us=50 encoder_age_us=900 settled={settled} weak=1 regulation=voltage '
                f'network=LINK_UP dual=1 phase_trip_enabled=0' + extra)

    def _handle(self, i, data, peer):
        cmd = parse(data, self.legacy)
        if cmd is None:
            self.rejected += 1
            return 'ERR syntax'
        session, sequence, op, value = cmd
        a, now = self.axes[i], time.monotonic()
        if op == 'DISABLE':
            self._stop_all()
            return 'OK DISABLED'
        if op == 'STATUS':
            return self._status(a)
        if op in ('CONFIG', 'TESTINFO', 'CALIBRATE', 'ZERO'):
            return 'ERR unsupported-in-fake'
        if a.owner != (peer[0], peer[1], session):
            inhibited = a.state in (OFF, READY)
            if op != 'POSITION' or not inhibited or (a.owner and now - a.owner_s <= LEASE_S):
                return 'ERR owner'
            a.owner, a.owner_seq = (peer[0], peer[1], session), (sequence - 1) & 0xFFFFFFFF
        if not 0 < ((sequence - a.owner_seq) & 0xFFFFFFFF) < 0x80000000:
            return 'ERR sequence'
        if op == 'TARGET':
            ok = True
            self.target_detected, self.target_s = bool(value), now
            self.targets += 1
        elif op == 'POSITION':
            ok = value <= MAX_MDEG[i] and a.state in (READY, WAKE_RUN, RUNNING)
            if a.state == RUNNING and now - a.command_s > LEASE_S:
                ok = False
            if ok:
                a.target, a.command_s, a.valid = value / 1000, now, True
        else:  # HEARTBEAT
            ok = a.valid and a.state != FAULT and now - a.command_s <= LEASE_S
            if ok:
                a.command_s = now
        if ok:
            a.owner_seq, a.owner_s = sequence, now
        return f"{'OK' if ok else 'ERR'} G1 {session} {sequence}"

    def _serve(self, i):
        sock = self.socks[i]
        while not self.stop_event.is_set():
            try:
                data, peer = sock.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            except OSError:
                return
            with self.lock:
                reply = self._handle(i, data, peer)
            if self.latency:
                time.sleep(self.latency)
            try:
                sock.sendto(reply.encode('ascii'), peer)
            except OSError:
                pass


def main():
    board = FakeBoard(ports=(5005, 5006)).start()
    print('Fake board on 127.0.0.1:5005 (tilt) and :5006 (pan). Enter = S1, "s" + Enter = S2, Ctrl-C quits.')
    try:
        while True:
            line = input()
            board.press_s2() if line.strip().lower() == 's' else board.press_s1()
            print('S2 pressed' if line.strip().lower() == 's' else 'S1 pressed')
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        board.close()


if __name__ == '__main__':
    main()
