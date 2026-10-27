"""One command to run the gimbal: check/restore calibration, start camera + detector,
wait for S1, track until Ctrl-C or S2.

    py -3 -m gimbal_bridge.run_gimbal --config gimbal_config.json
    py -3 -m gimbal_bridge.run_gimbal --synthetic        # no camera: rendered ball, real board
    py -3 -m gimbal_bridge.run_gimbal --preview          # show the camera with the detection

Order of events:
  1. Connect to both axes; print what the board restored from flash.
  2. Calibrate/home/zero whatever is missing (calibration.py). Skip with --skip-calibration.
  3. Open the camera (or synthetic camera) and the detector.
  4. Hold the current position, ask for S1, then track.
Ctrl-C (or closing the preview with q) sends DISABLE. S2 on the board stops everything.
"""
import argparse
import copy
import json
from datetime import datetime
from pathlib import Path
import threading
import time

from .bridge import Bridge, BridgeError
from .calibration import CalibrationError, ConsoleUI, ensure_ready
from .camera_model import Intrinsics
from .g1 import G1Client, G1Error, SYSTEM_NAMES
from .tracking import TrackerConfig

DEFAULTS = {
    'board': {'host': '192.168.10.2', 'tilt_port': 5005, 'pan_port': 5006},
    'tracking': {'pan_sign': -1, 'tilt_sign': 1, 'gain': 0.8, 'deadband_deg': 0.5, 'max_step_deg': 20.0,
                 'tilt_limits': [15.0, 345.0], 'pan_limits': [15.0, 320.0], 'stale_s': 0.3, 'rate_hz': 50.0},
    'camera': {'source': 0, 'width': 640, 'height': 480, 'fps': 30, 'intrinsics': None, 'hfov_deg': 70.0},
    'detector': {'type': 'color', 'hsv_ranges': [[[20, 80, 80], [40, 255, 255]]], 'min_area': 60,
                 'min_circularity': 0.6},
    'aim_offset_deg': {'pan': 0.0, 'tilt': 0.0},
    'synthetic': {'amplitude_deg': 10.0, 'period_s': 8.0, 'dropout': 0.0},
    # Where S1 first sends the gimbal: 'center' of the tracking windows, {"tilt": .., "pan": ..},
    # or null to start from wherever it is (must then already be inside the windows).
    'start_pose': 'center',
    'arm_timeout_s': 60.0,
    'log_dir': 'logs/run',
}


def merge(base, override):
    out = copy.deepcopy(base)
    for key, value in (override or {}).items():
        out[key] = merge(out[key], value) if isinstance(value, dict) and isinstance(out.get(key), dict) else value
    return out


def load_config(path=None):
    if not path:
        return copy.deepcopy(DEFAULTS)
    with open(path, encoding='utf-8') as fh:
        return merge(DEFAULTS, json.load(fh))


def tracker_config(cfg):
    t = cfg['tracking']
    return TrackerConfig(pan_sign=t['pan_sign'], tilt_sign=t['tilt_sign'], gain=t['gain'],
                         deadband_deg=t['deadband_deg'], max_step_deg=t['max_step_deg'],
                         tilt_limits=tuple(t['tilt_limits']), pan_limits=tuple(t['pan_limits']), stale_s=t['stale_s'])


def start_pose(cfg):
    pose, t = cfg.get('start_pose'), cfg['tracking']
    if pose is None:
        return None
    if pose == 'center':
        return tuple(sum(t[f'{name}_limits']) / 2 for name in ('tilt', 'pan'))
    return float(pose['tilt']), float(pose['pan'])


def intrinsics(cfg, ui=None):
    c = cfg['camera']
    if c.get('intrinsics') and Path(c['intrinsics']).exists():
        return Intrinsics.load(c['intrinsics'])
    if c.get('intrinsics') and ui:
        ui.info(f"WARNING: {c['intrinsics']} not found (run gimbal_bridge.camcal); using a {c['hfov_deg']}-degree "
                'field-of-view estimate, which is not accurate enough for aiming.')
    return Intrinsics.from_fov(c['width'], c['height'], c['hfov_deg'])


def run(cfg, synthetic=False, skip_calibration=False, preview=False, ui=None, stop_event=None, status_every_s=0.5):
    """Run until stop_event is set, the bridge stops (S2, fault, link) or Ctrl-C. Returns a summary dict."""
    from .detector import make_detector
    ui = ui or ConsoleUI()
    stop_event = stop_event or threading.Event()
    b = cfg['board']
    clients = [G1Client(b['host'], b['tilt_port'], 0.1), G1Client(b['host'], b['pan_port'], 0.1)]
    source = loop = bridge = None
    summary = {}
    try:
        if not skip_calibration:
            summary['calibration'] = ensure_ready(clients, ui)
        k = intrinsics(cfg, ui)
        detector = make_detector(cfg['detector'])
        log_dir = Path(cfg['log_dir'])
        log_dir.mkdir(parents=True, exist_ok=True)
        log_path = log_dir / f"run_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        bridge = Bridge(tracker_config(cfg), rate_hz=cfg['tracking']['rate_hz'], log_path=log_path, clients=clients)
        # Open the camera before arming, so a missing camera fails before anything moves.
        if synthetic:
            from .camera import SyntheticCamera
            s = cfg['synthetic']
            t = cfg['tracking']
            source = SyntheticCamera(k, bridge.positions, (t['tilt_sign'], t['pan_sign']), s['amplitude_deg'],
                                     s['period_s'], cfg['camera']['fps'], dropout=s['dropout'])
        else:
            from .camera import CameraSource
            c = cfg['camera']
            source = CameraSource(c['source'], c['width'], c['height'], c['fps'])
        pose = start_pose(cfg)
        where = 'stays where it is' if pose is None else f'moves to tilt {pose[0]:.1f}, pan {pose[1]:.1f} deg'
        bridge.start(cfg['arm_timeout_s'], lambda: ui.wait_for_s1(
            f'Press S1 on the board to start tracking: the gimbal {where}, then follows the ball. '
            'S2 or Ctrl-C stops.'), start_pose=pose)
        on_frame = _preview(stop_event) if preview else None
        from .vision import VisionLoop
        offset = cfg['aim_offset_deg']
        loop = VisionLoop(source, detector, k, bridge, (offset['pan'], offset['tilt']), on_frame).start()
        ui.info(f'Tracking. Log: {log_path}')
        next_status = 0.0
        while not stop_event.is_set() and bridge.running() and loop.running():
            if time.monotonic() >= next_status and status_every_s:
                st, (tilt, pan) = loop.stats(), (bridge.axes[0], bridge.axes[1])
                ui.info(f"{bridge.mode:9s} board {SYSTEM_NAMES.get(bridge.system_state, '-'):10s} "
                        f"tilt {tilt.measured_deg:7.2f}->{tilt.target_deg:7.2f} pan {pan.measured_deg:7.2f}->"
                        f"{pan.target_deg:7.2f}  {st['fps']:4.1f} fps  ball {st['detection_rate']:.0%}  "
                        f"detect {st['detect_ms']:.1f} ms  pipeline {st['pipeline_ms']:.1f} ms")
                next_status = time.monotonic() + status_every_s
            time.sleep(0.02)
        if loop.error:
            raise loop.error
        if bridge.error:
            raise bridge.error
        summary['vision'] = loop.stats()
        return summary
    finally:
        if loop:
            loop.stop()
        if bridge:
            bridge.close()          # DISABLE both axes
        else:
            for c in clients:
                try:
                    c.disable()
                except (OSError, RuntimeError, TimeoutError):
                    pass
        if source:
            source.close()
        for c in clients:
            c.close()
        if preview:
            import cv2
            cv2.destroyAllWindows()


def _preview(stop_event):
    import cv2

    def show(frame, det, obs):
        view = frame.copy()
        h, w = view.shape[:2]
        cv2.drawMarker(view, (w // 2, h // 2), (255, 255, 255), cv2.MARKER_CROSS, 20, 1)
        if det:
            cv2.circle(view, (int(det.cx), int(det.cy)), int(max(det.w, det.h) / 2), (0, 255, 0), 2)
            cv2.putText(view, f'pan {obs.pan_error_deg:+.1f}  tilt {obs.tilt_error_deg:+.1f} deg', (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
        cv2.imshow('gimbal', view)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            stop_event.set()
    return show


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--config', help='JSON file; see gimbal_config.example.json')
    p.add_argument('--host', help='override board.host')
    p.add_argument('--synthetic', action='store_true', help='rendered ball instead of the camera')
    p.add_argument('--skip-calibration', action='store_true')
    p.add_argument('--preview', action='store_true')
    a = p.parse_args()
    cfg = load_config(a.config)
    if a.host:
        cfg['board']['host'] = a.host
    try:
        summary = run(cfg, a.synthetic, a.skip_calibration, a.preview)
        print(f'Finished: {summary}')
    except KeyboardInterrupt:
        print('Stopped; both axes disabled.')
    except (BridgeError, CalibrationError, G1Error, TimeoutError, RuntimeError) as exc:
        print(f'STOPPED: {exc}')
        raise SystemExit(1)


if __name__ == '__main__':
    main()
