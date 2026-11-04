"""Start electrical calibration over Ethernet (the shaft WILL move).

Requires motion-enabled Ethernet firmware, stopped in OFF or READY. Prompts for
Enter to capture mechanical zero with PWM off; never arms position control.
S2/Ctrl-C stops. No packages needed.
"""
import argparse
import math
import time
from position_client import Client


def run(client, axis, timeout):
    initial = client.request('STATUS')
    print(initial, flush=True)
    if initial['axis'] != axis:
        raise RuntimeError('Selected firmware axis does not match --axis')
    if not (initial['source'] == 1 and initial['enabled'] and not initial['fault']
            and initial['state'] in (0, 5)):
        raise RuntimeError('Requires Ethernet source, motion enabled, stopped in OFF or READY')
    attempted = False
    started = False
    try:
        attempted = True
        print(client.request('CALIBRATE'), flush=True)
        print('Calibration requested: shaft will align, sweep and return. S2/Ctrl-C stops.', flush=True)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = client.request('STATUS')
            if status['fault']:
                raise RuntimeError(f'Calibration fault: {status}')
            if status['axis'] != axis or status['source'] != 1:
                raise RuntimeError('Axis or command source changed')
            started |= status['state'] in (1, 2, 3, 4)
            print(f"state={status['state']} calibrated={status['calibrated']} zero={status['zero']}", flush=True)
            if started and status['state'] == 5 and status['calibrated']:
                print('PASS: electrical calibration complete. Set mechanical zero separately with S2.', flush=True)
                return
            if started and status['state'] not in (1, 2, 3, 4):
                raise RuntimeError('Calibration stopped before completing')
            try:
                client.request('HEARTBEAT')
            except RuntimeError:
                # Completion can invalidate the lease between STATUS and this
                # heartbeat. Let the next STATUS establish success or failure.
                final = client.request('STATUS')
                if not (started and final['state'] == 5 and final['calibrated'] and not final['fault']):
                    raise
                print('PASS: electrical calibration complete. Set mechanical zero separately with S2.', flush=True)
                return
            time.sleep(0.08)
        raise TimeoutError('Calibration timed out')
    finally:
        if attempted:
            try:
                print(client.request('DISABLE'), flush=True)
            except (OSError, RuntimeError, ValueError, TimeoutError):
                print('DISABLE not acknowledged; the calibration lease expires after 500 ms.', flush=True)


def keyboard_zero(client, axis):
    print('Electrical calibration finished; PWM is OFF.', flush=True)
    input('Place the shaft at your physical 0-degree position, release it, then press Enter to set zero: ')
    status = client.request('STATUS')
    if not (status['axis'] == axis and status['source'] == 1 and status['state'] == 5
            and status['calibrated'] and not status['fault']):
        raise RuntimeError('Zero capture requires calibrated SERVO_READY with no fault')
    print(client.request('ZERO'), flush=True)
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        status = client.request('STATUS')
        if status['fault'] or status['state'] != 5:
            raise RuntimeError(f'Zero capture interrupted: {status}')
        if status['zero'] and abs(status['position_mdeg']) <= 1000:
            print('PASS: mechanical zero set. READY; run the position/sweep script and press S1 to move.', flush=True)
            return
        time.sleep(0.08)
    raise TimeoutError('Mechanical zero was not confirmed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.10.2')
    parser.add_argument('--port', type=int, default=5005)
    parser.add_argument('--axis', type=int, choices=(0, 1), default=0)
    parser.add_argument('--timeout', type=float, default=20)
    parser.add_argument('--skip-zero', action='store_true', help='Electrical calibration only; omit Enter prompt')
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('timeout must be positive and finite')
    client = Client(args.host, args.port)
    try:
        run(client, args.axis, args.timeout)
        if not args.skip_zero:
            keyboard_zero(client, args.axis)
    finally:
        client.close()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Stopped by user.')
        raise SystemExit(130)
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f'FAIL: {error}')
        raise SystemExit(1)
