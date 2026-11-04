"""Two bounded round-trip sweeps with random moves between them; arm once with S1.

Requires Ethernet-source voltage firmware, completed calibration/zero, READY.
Standard library only. S2 or Ctrl-C stops; completion/error sends DISABLE.
"""
import argparse
import math
import random
import time
from pathlib import Path

from position_client import Client
from position_tuning import Recorder, read_config


def controller_fault(status):
    names = {1: 'SERVO_DRIVER', 2: 'SERVO_FEEDBACK', 3: 'SERVO_ZERO_TIMEOUT',
             4: 'SERVO_CALIBRATION', 5: 'SERVO_CURRENT', 6: 'SERVO_CURRENT_SUM',
             7: 'SERVO_TRAVEL', 8: 'SERVO_FOLLOWING', 9: 'SERVO_COMMAND_TIMEOUT',
             10: 'SERVO_SUPPLY', 11: 'SERVO_TEMPERATURE', 12: 'SERVO_TIMING',
             13: 'SERVO_FOREGROUND', 14: 'SERVO_INTERNAL'}
    fault = status['fault']
    message = f'Controller fault={fault} ({names.get(fault, "UNKNOWN")}): {status}'
    if fault == 7:
        message += ('; inspect travel/overspeed. If position is outside -0.5..360.5 degrees '
                    'before motion, reset and recapture zero with the shaft released and stationary.')
    return message


def targets(low, high, count, seed):
    rng = random.Random(seed)
    return [low, high, low, *[rng.uniform(low, high) for _ in range(count)],
            low, high, low]


def run(client, axis, positions, arm_timeout, move_timeout, observer=None):
    emit = observer or (lambda event, **data: None)
    initial = client.request('STATUS')
    emit('initial', status=initial)
    print(initial, flush=True)
    if initial['axis'] != axis:
        raise RuntimeError('Selected firmware axis does not match --axis')
    if not (initial['source'] == 1 and initial['enabled'] and initial['calibrated']
            and initial['zero'] and initial['state'] == 5 and not initial['fault']):
        raise RuntimeError('Requires Ethernet source, motion enabled, calibrated/zeroed and SERVO_READY')
    attempted = False
    running_seen = False
    try:
        for number, degrees in enumerate(positions, 1):
            target = round(degrees * 1000)
            attempted = True
            print(client.request('POSITION', target), flush=True)
            emit('move', number=number, target=target)
            print(f'Move {number}/{len(positions)}: {target/1000:.2f} deg', flush=True)
            if not running_seen:
                print('Press physical S1 once to start the entire sequence. S2/Ctrl-C stops.', flush=True)
            deadline = time.monotonic() + (move_timeout if running_seen else arm_timeout)
            next_report = 0.0
            stable_since = None
            stable_min = stable_max = 0
            while True:
                try:
                    client.request('HEARTBEAT')
                except RuntimeError as error:
                    status = client.request('STATUS')
                    emit('sample', status=status)
                    if status['fault']:
                        raise RuntimeError(controller_fault(status)) from error
                    raise RuntimeError(f'Heartbeat rejected; fault={status["fault"]}, status={status}') from error
                status = client.request('STATUS')
                emit('sample', status=status)
                now = time.monotonic()
                if status['fault']:
                    raise RuntimeError(controller_fault(status))
                if status['axis'] != axis or status['source'] != 1:
                    raise RuntimeError('Axis or command source changed')
                if status['state'] == 7 and not running_seen:
                    running_seen = True
                    deadline = now + move_timeout
                if running_seen and status['state'] != 7:
                    raise RuntimeError('Motor stopped; sequence cancelled (no automatic rearm)')
                if not running_seen and status['state'] not in (5, 6):
                    raise RuntimeError('Controller left READY/WAKE_RUN before arming')
                if now >= next_report:
                    print(f"  state={status['state']} measured={status['position_mdeg']/1000:.2f} deg "
                          f"rpm={status['rpm_milli']/1000:.2f}", flush=True)
                    next_report = now + 0.5
                # Match the echoed target too: a queued reply for the previous
                # settled move cannot qualify the newly requested position.
                # The firmware flag requires <0.5 RPM continuously and can
                # chatter with encoder velocity-estimator noise at rest. Use
                # a measured 0.5-s dwell here: within 2.5 degrees of target,
                # <=1 degree peak-to-peak movement and <=2 RPM throughout.
                position = status['position_mdeg']
                qualified = (running_seen and status['target_mdeg'] == target
                             and abs(position - target) <= 2500
                             and abs(status['rpm_milli']) <= 2000)
                if not qualified:
                    stable_since = None
                else:
                    if stable_since is None:
                        stable_since = now
                        stable_min = stable_max = position
                    stable_min = min(stable_min, position)
                    stable_max = max(stable_max, position)
                    if stable_max - stable_min > 1000:
                        stable_since = now
                        stable_min = stable_max = position
                    if now - stable_since >= 0.5:
                        print(f'  PASS move {number}: encoder held target for 0.5 s.', flush=True)
                        emit('complete')
                        break
                if now >= deadline:
                    raise TimeoutError('Move/arm timed out; sequence cancelled')
                time.sleep(0.08)
        print('PASS: both round-trip sweeps and intermediate moves completed.', flush=True)
    finally:
        if attempted:
            try:
                print(client.request('DISABLE'), flush=True)
            except (OSError, RuntimeError, TimeoutError, ValueError):
                print('DISABLE not acknowledged; the local 500 ms command lease expires.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.10.2')
    parser.add_argument('--port', type=int, default=5005)
    parser.add_argument('--axis', type=int, choices=(0, 1), default=0)
    parser.add_argument('--min-deg', type=float, default=2)
    parser.add_argument('--max-deg', type=float, default=358)
    parser.add_argument('--random-moves', type=int, default=6)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--arm-timeout', type=float, default=60)
    parser.add_argument('--move-timeout', type=float, default=30)
    parser.add_argument('--tune', action='store_true', help='Short repeatable bidirectional 10/45/180-degree moves')
    parser.add_argument('--log-dir', default='logs', help='CSV and JSON output folder')
    parser.add_argument('--config', type=Path, default=Path(__file__).resolve().parents[1]/'Core/Inc/control_config.h',
                        help='Local tuning labels; must match flashed firmware')
    args = parser.parse_args()
    if not (math.isfinite(args.min_deg) and math.isfinite(args.max_deg)
            and 2 <= args.min_deg < args.max_deg <= 358):
        parser.error('Require 2 <= min-deg < max-deg <= 358 (no boundary crossing)')
    if not 0 <= args.random_moves <= 100:
        parser.error('random-moves must be between 0 and 100')
    if not all(math.isfinite(t) and t > 0 for t in (args.arm_timeout, args.move_timeout)):
        parser.error('Timeouts must be positive and finite')
    positions = ([90, 100, 90, 135, 90, 270, 90] if args.tune else
                 targets(args.min_deg, args.max_deg, args.random_moves, args.seed))
    print('Planned degrees:', ', '.join(f'{p:.2f}' for p in positions), flush=True)
    recorder = Recorder(args.log_dir, read_config(args.config,axis=args.axis), positions)
    client = None
    failure = None
    try:
        client = Client(args.host, args.port)
        run(client, args.axis, positions, args.arm_timeout, args.move_timeout, recorder)
    except BaseException as error:
        failure = f'{type(error).__name__}: {error}'
        raise
    finally:
        if client is not None:
            client.close()
        recorder.finish(failure)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Stopped by user.')
        raise SystemExit(130)
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f'FAIL: {error}')
        raise SystemExit(1)
