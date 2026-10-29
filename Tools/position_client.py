"""UDP bench client. POSITION is bounded and never arms the bridge.

Laptop NIC: 192.168.10.1/24. Board: 192.168.10.2 UDP 5005.
Use --position 90, then press physical S1 after calibration/zeroing. The client
keeps the command lease alive and checks measured position, rather than treating
an ACK as motion success. Ctrl-C, completion, timeout and errors send DISABLE.
Python standard library only.
"""
import argparse
import math
import secrets
import socket
import time


def status_fields(text):
    if not text.startswith('S1 '):
        raise ValueError(f'Unexpected status: {text}')
    fields = dict(item.split('=', 1) for item in text.split()[1:])
    for name in ('axis', 'source', 'enabled', 'state', 'fault', 'calibrated', 'zero',
                 'position_mdeg', 'target_mdeg', 'rpm_milli', 'settled'):
        fields[name] = int(fields[name])
    return fields


class Client:
    def __init__(self, host, port, timeout=0.3):
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('timeout must be positive and finite')
        self.timeout = timeout
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.connect((host, port))
            self.sock.settimeout(timeout)
        except BaseException:
            self.sock.close()
            raise
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0

    def request(self, operation, value=None):
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        packet = f'G1 {self.session} {self.sequence} {operation}'
        if value is not None:
            packet += f' {value}'
        self.sock.send(packet.encode('ascii'))
        expected = f'OK G1 {self.session} {self.sequence}'
        deadline = time.monotonic() + self.timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('No matching response')
            self.sock.settimeout(remaining)
            # HEALTH includes driver, ADC and network counters in one datagram.
            text = self.sock.recv(4096).decode('ascii')
            if text.startswith('ERR'):
                raise RuntimeError(text)
            if operation == 'STATUS' and text.startswith('S1 '):
                return status_fields(text)
            prefix = {'CONFIG': 'C1 ', 'DIAG': 'D1 ', 'HEALTH': 'H1 '}.get(operation)
            if prefix is not None and text.startswith(prefix):
                fields=dict(item.split('=',1) for item in text.split()[1:])
                for key,value in fields.items():
                    try:fields[key]=int(value)
                    except ValueError:pass
                return fields
            if operation == 'DISABLE' and text == 'OK DISABLED':
                return text
            if operation not in ('STATUS', 'CONFIG', 'DIAG', 'HEALTH', 'DISABLE') and text == expected:
                return text

    def close(self):
        self.sock.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.10.2')
    p.add_argument('--port', default=5005, type=int)
    p.add_argument('--position', type=float, help='Degrees in [0,360]; never wraps')
    p.add_argument('--axis', type=int, choices=(0, 1), default=0)
    p.add_argument('--timeout', type=float, default=30, help='Includes time to press S1')
    args = p.parse_args()
    if args.position is not None and (not math.isfinite(args.position) or not 0 <= args.position <= 360):
        p.error('--position must be finite and within 0..360')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        p.error('--timeout must be positive and finite')
    client = Client(args.host, args.port)
    motion_attempted = False
    try:
        initial = client.request('STATUS')
        print(initial, flush=True)
        if args.position is None:
            return
        if initial['axis'] != args.axis:
            raise RuntimeError('Selected firmware axis does not match --axis')
        if not (initial['source'] == 1 and initial['enabled'] and initial['calibrated'] and initial['zero']):
            raise RuntimeError('Requires Ethernet-source motion build, commutation calibration and mechanical zero')
        if initial['fault'] or initial['state'] != 5:  # SERVO_READY, inhibited
            raise RuntimeError('Start from SERVO_READY; use S2 to stop first')
        target = round(args.position * 1000)
        motion_attempted = True
        print(client.request('POSITION', target), flush=True)
        print('Command accepted. Press physical S1 to arm. Ctrl-C stops.', flush=True)
        deadline = time.monotonic() + args.timeout
        running_seen = False
        while time.monotonic() < deadline:
            client.request('HEARTBEAT')
            s = client.request('STATUS')
            if s['fault']:
                raise RuntimeError(f"Controller fault {s['fault']}: {s}")
            running_seen |= s['state'] == 7  # SERVO_RUNNING
            print(f"state={s['state']} measured={s['position_mdeg']/1000:.2f} deg "
                  f"rpm={s['rpm_milli']/1000:.2f}", flush=True)
            if (running_seen and s['state'] == 7 and s['settled'] and
                    abs(s['position_mdeg'] - target) <= 2500):
                print('PASS: measured encoder position settled within 2.5 degrees.', flush=True)
                return
            if running_seen and s['state'] not in (6, 7):
                raise RuntimeError('Controller inhibited before reaching the target')
            time.sleep(0.08)
        raise TimeoutError('Motor did not settle before timeout')
    finally:
        if motion_attempted:
            try:
                print(client.request('DISABLE'), flush=True)
            except (OSError, RuntimeError, TimeoutError, ValueError):
                print('DISABLE not acknowledged; the 500 ms command lease expires locally.', flush=True)
        client.close()


if __name__ == '__main__':
    main()
