"""Run the tracking bridge against the PCB.

Inputs:
  --input synthetic  Fake ball moving in a slow figure-8; the error is computed from the
                     measured gimbal position, so the real motors close the loop without
                     a camera. Use this first to confirm axis signs and tuning.
  --input udp        Vision process sends one JSON object per datagram to 127.0.0.1:5100:
                     {"detected": true, "pan_error_deg": 1.5, "tilt_error_deg": -0.4}
                     (optional "t_capture": time.monotonic() value from the same machine).

Before running: both axes calibrated and zeroed (Tools/calibrate_motor.py), READY, and the
laptop NIC at 192.168.10.1/24. Press physical S1 when prompted. S2 or Ctrl-C stops both.

    py -3 -m gimbal_bridge.track --input synthetic --amplitude 10
"""
import argparse
import json
import math
import random
import socket
import threading
import time
from datetime import datetime
from pathlib import Path

from .bridge import Bridge, BridgeError
from .g1 import DEFAULT_HOST, TILT_PORT, PAN_PORT
from .tracking import Observation, TrackerConfig


def synthetic_source(bridge, stop, centre, amplitude, period_s, fps, dropout, signs):
    """Ball moves in gimbal coordinates; the 'camera' reports where it is relative to the gimbal."""
    t0 = time.monotonic()
    while not stop.is_set():
        t = time.monotonic() - t0
        ball = (centre[0] + amplitude / 2 * math.sin(4 * math.pi * t / period_s),
                centre[1] + amplitude * math.sin(2 * math.pi * t / period_s))
        tilt, pan = bridge.positions()
        # Inverse of tracking.next_target: error = (ball - position) / sign, and sign is +/-1.
        err_tilt, err_pan = (ball[0] - tilt) * signs[0], (ball[1] - pan) * signs[1]
        detected = random.random() >= dropout
        bridge.submit(Observation(detected, err_pan, err_tilt))
        time.sleep(1 / fps)


def udp_source(bridge, stop, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', port))
    sock.settimeout(0.2)
    while not stop.is_set():
        try:
            data = json.loads(sock.recv(2048))
            bridge.submit(Observation(bool(data['detected']), float(data.get('pan_error_deg', 0)),
                                      float(data.get('tilt_error_deg', 0)), data.get('t_capture')))
        except socket.timeout:
            continue
        except (ValueError, KeyError, TypeError) as exc:
            print(f'ignored bad vision packet: {exc}', flush=True)
    sock.close()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host', default=DEFAULT_HOST)
    p.add_argument('--tilt-port', type=int, default=TILT_PORT)
    p.add_argument('--pan-port', type=int, default=PAN_PORT)
    p.add_argument('--input', choices=('synthetic', 'udp'), default='synthetic')
    p.add_argument('--vision-port', type=int, default=5100)
    p.add_argument('--amplitude', type=float, default=10.0, help='synthetic: pan swing in degrees')
    p.add_argument('--period', type=float, default=8.0, help='synthetic: seconds per cycle')
    p.add_argument('--fps', type=float, default=30.0, help='synthetic: observation rate')
    p.add_argument('--dropout', type=float, default=0.0, help='synthetic: fraction of frames with no detection')
    p.add_argument('--pan-sign', type=int, choices=(-1, 1), default=-1)
    p.add_argument('--tilt-sign', type=int, choices=(-1, 1), default=1)
    p.add_argument('--gain', type=float, default=0.8)
    p.add_argument('--deadband', type=float, default=0.5)
    p.add_argument('--max-step', type=float, default=20.0)
    p.add_argument('--tilt-min', type=float, default=15.0)
    p.add_argument('--tilt-max', type=float, default=345.0)
    p.add_argument('--pan-min', type=float, default=15.0)
    p.add_argument('--pan-max', type=float, default=320.0)
    p.add_argument('--rate', type=float, default=50.0, help='bridge loop rate, Hz')
    p.add_argument('--arm-timeout', type=float, default=60.0)
    p.add_argument('--log-dir', type=Path, default=Path('logs/track'))
    a = p.parse_args()

    cfg = TrackerConfig(pan_sign=a.pan_sign, tilt_sign=a.tilt_sign, gain=a.gain, deadband_deg=a.deadband,
                        max_step_deg=a.max_step, tilt_limits=(a.tilt_min, a.tilt_max),
                        pan_limits=(a.pan_min, a.pan_max))
    a.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = a.log_dir / f"track_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    bridge = Bridge(cfg, a.host, a.tilt_port, a.pan_port, rate_hz=a.rate, log_path=log_path)
    stop = threading.Event()
    try:
        bridge.start(a.arm_timeout, lambda: print('Holding current position. Press physical S1 to arm '
                                                  'both axes. S2 or Ctrl-C stops.', flush=True))
        tilt0, pan0 = bridge.positions()
        if a.input == 'synthetic':
            source = threading.Thread(target=synthetic_source, daemon=True, args=(
                bridge, stop, (tilt0, pan0), a.amplitude, a.period, a.fps, a.dropout, (a.tilt_sign, a.pan_sign)))
        else:
            source = threading.Thread(target=udp_source, daemon=True, args=(bridge, stop, a.vision_port))
        source.start()
        print(f'Tracking ({a.input}). Logging to {log_path}', flush=True)
        while bridge.running():
            t, pn = bridge.axes
            print(f'{bridge.mode:9s} tilt {t.measured_deg:7.2f} -> {t.target_deg:7.2f}   '
                  f'pan {pn.measured_deg:7.2f} -> {pn.target_deg:7.2f}', flush=True)
            time.sleep(0.5)
        if bridge.error:
            raise bridge.error
    except KeyboardInterrupt:
        print('Stopping.', flush=True)
    except BridgeError as exc:
        print(f'STOPPED: {exc}', flush=True)
        raise SystemExit(1)
    finally:
        stop.set()
        bridge.close()
        print('Both axes disabled.', flush=True)


if __name__ == '__main__':
    main()
