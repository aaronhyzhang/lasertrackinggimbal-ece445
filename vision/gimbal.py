"""Vision-to-G1 bridge for both PCB_CONTROL branches; no firmware changes.

Derived from the host bridge on origin/laptop-bridge: one session per axis,
absolute POSITION commands, physical S1, and an independent lease/feedback loop.
All socket operations belong to one worker. Vision only submits observations.
"""
from collections import deque
from dataclasses import dataclass
import argparse
import json
import math
from pathlib import Path
import threading
import time

from Tools.position_client import Client

CONFIG_PATH = Path(__file__).with_name("gimbal.json")
AXES = ("tilt", "pan")
READY, WAKING, RUNNING = 5, 6, 7


@dataclass(frozen=True)
class Settings:
    host: str = "192.168.10.2"
    tilt_port: int = 5005
    pan_port: int = 5006
    pan_sign: int = 1
    tilt_sign: int = 1
    gain: float = 0.4
    max_step_deg: float = 3.0
    min_update_deg: float = 0.05
    stale_s: float = 0.3
    loss_stop_s: float = 2.0
    arm_timeout_s: float = 60.0
    capture_delay_s: float = 0.0
    # Motor degrees per gimbal degree; 1 for direct coupling.
    pan_ratio: float = 1.0
    tilt_ratio: float = 1.0
    # Optional measured level pose enables coupled yaw/elevation geometry.
    tilt_level_deg: float | None = None
    # Optional narrower mechanical windows, intersected with firmware CONFIG.
    tilt_limits: tuple | None = None
    pan_limits: tuple | None = None

    def __post_init__(self):
        if not isinstance(self.host, str) or not self.host:
            raise ValueError("Gimbal host is required")
        if self.pan_sign not in (-1, 1) or self.tilt_sign not in (-1, 1):
            raise ValueError("Gimbal signs must be +1 or -1")
        if any(type(p) is not int or not 1 <= p <= 65535 for p in (self.tilt_port, self.pan_port)) or self.tilt_port == self.pan_port:
            raise ValueError("Use distinct valid tilt/pan UDP ports")
        for name in ("gain", "max_step_deg", "min_update_deg", "stale_s", "loss_stop_s",
                     "arm_timeout_s", "capture_delay_s", "pan_ratio", "tilt_ratio"):
            value = getattr(self, name)
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{name} must be finite")
        if not 0 < self.gain <= 1 or not 0 < self.max_step_deg <= 10 or self.min_update_deg < 0:
            raise ValueError("Require 0 < gain <= 1, 0 < max_step_deg <= 10, min_update_deg >= 0")
        if not 0 < self.stale_s <= 0.5 or self.loss_stop_s < self.stale_s or self.arm_timeout_s <= 0:
            raise ValueError("Invalid stale/loss/arming timeout")
        if self.capture_delay_s < 0 or self.capture_delay_s >= self.stale_s or min(self.pan_ratio, self.tilt_ratio) <= 0:
            raise ValueError("Invalid capture delay or gear ratios")
        if self.tilt_level_deg is not None and not math.isfinite(self.tilt_level_deg):
            raise ValueError("tilt_level_deg must be finite or null")
        for limits in (self.tilt_limits, self.pan_limits):
            if limits is not None and (len(limits) != 2 or not all(math.isfinite(v) for v in limits) or limits[0] >= limits[1]):
                raise ValueError("Axis limits must be [minimum, maximum]")

    @classmethod
    def load(cls, path=CONFIG_PATH):
        with Path(path).open(encoding="utf-8") as source:
            values = json.load(source)
        try:
            return cls(**values)
        except TypeError as error:
            raise ValueError(f"Invalid gimbal configuration: {error}") from error


def target_angles(pose, current, pan_error, tilt_error, settings, limits):
    """Return (tilt, pan) absolute motor targets, never wrapped across an end stop.

    pose is the estimated encoder pose at frame receipt minus capture_delay_s.
    current is the newest encoder pose. Resolve the observed target from pose,
    then move a bounded fraction of the remaining error from current. This avoids
    adding an old image error to a newer pose. Default independent-axis geometry
    is a local approximation for an upright, aligned camera near level. Supplying
    the measured tilt_level_deg enables the full ray rotation for a yaw/pitch
    gimbal (orthogonal axes, camera aligned with the tilt frame, no camera roll).
    """
    values = (*pose, *current, pan_error, tilt_error)
    if not all(math.isfinite(v) for v in values) or max(abs(pan_error), abs(tilt_error)) >= 89:
        raise ValueError("Invalid camera angles or encoder pose")
    s = settings
    tilt_step, pan_step = tilt_error, pan_error
    if s.tilt_level_deg is not None:
        elevation = math.radians(s.tilt_sign * (pose[0] - s.tilt_level_deg) / s.tilt_ratio)
        if abs(elevation) >= math.radians(80):
            raise ValueError("Coupled tracking requires camera elevation within +/-80 degrees")
        x, y = math.tan(math.radians(pan_error)), math.tan(math.radians(tilt_error))
        base_y = math.cos(elevation) * y + math.sin(elevation)
        base_z = math.cos(elevation) - math.sin(elevation) * y
        pan_step = math.degrees(math.atan2(x, base_z))
        tilt_step = math.degrees(math.atan2(base_y, math.hypot(x, base_z)) - elevation)
    goals = (pose[0] + s.tilt_sign * s.tilt_ratio * tilt_step,
             pose[1] + s.pan_sign * s.pan_ratio * pan_step)
    targets = []
    for goal, measured, ratio, (low, high) in zip(goals, current, (s.tilt_ratio, s.pan_ratio), limits):
        # Clamp the absolute goal before limiting the correction. No modulo 360.
        goal = min(high, max(low, goal))
        step = min(s.max_step_deg * ratio, max(-s.max_step_deg * ratio, s.gain * (goal - measured)))
        targets.append(min(high, max(low, measured + step)))
    return tuple(targets)


def read_status(client, axis):
    status = client.request("STATUS")
    for key in ("axis", "dual", "source", "enabled", "state", "fault", "calibrated",
                "zero", "position_mdeg", "encoder_age_us"):
        try:
            status[key] = int(status[key])
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError(f"{AXES[axis]} STATUS missing/invalid {key}; requires dual PCB_CONTROL firmware") from error
    if status["axis"] != axis or status["dual"] != 1 or status["source"] != 1 or status["enabled"] != 1:
        raise RuntimeError(f"{AXES[axis]} endpoint is not the expected dual Ethernet motion axis: {status}")
    if status["fault"] or int(status.get("sys", 0)) == 6:
        raise RuntimeError(f"{AXES[axis]} firmware fault: {status}")
    if not status["calibrated"] or not status["zero"]:
        raise RuntimeError(f"{AXES[axis]} needs existing electrical calibration and mechanical zero")
    if not 0 <= status["encoder_age_us"] <= 4000:
        raise RuntimeError(f"{AXES[axis]} encoder feedback is stale")
    return status


def read_limits(client, axis, settings):
    config = client.request("CONFIG")
    if any(int(config.get(key, -1)) != value for key, value in (("axis", axis), ("dual", 1), ("source", 1))):
        raise RuntimeError(f"{AXES[axis]} CONFIG does not match the dual Ethernet axis")
    try:
        low, high = int(config["min_mdeg"]) / 1000, int(config["max_mdeg"]) / 1000
    except (KeyError, TypeError, ValueError) as error:
        raise RuntimeError(f"{AXES[axis]} CONFIG lacks travel limits") from error
    if not 0 <= low < high <= 360:
        raise RuntimeError(f"Invalid {AXES[axis]} firmware limits")
    narrower = (settings.tilt_limits, settings.pan_limits)[axis]
    if narrower is not None:
        low, high = max(low, narrower[0]), min(high, narrower[1])
    if low >= high:
        raise RuntimeError(f"{AXES[axis]} configured window does not overlap firmware limits")
    return low, high


def pose_at(history, timestamp):
    """Interpolate host-timestamped telemetry; never extrapolate motor velocity."""
    if not history:
        return None
    if timestamp < history[0][0]:
        return history[0][1] if history[0][0] - timestamp <= 0.075 else None
    for (ta, a), (tb, b) in zip(history, list(history)[1:]):
        if ta <= timestamp <= tb:
            if tb - ta > 0.15:
                return None
            return a if tb == ta else a + (b - a) * (timestamp - ta) / (tb - ta)
    return history[-1][1] if timestamp - history[-1][0] <= 0.075 else None


class Gimbal:
    """Single worker owns both Clients; update() never waits on Ethernet."""
    def __init__(self, settings=None, dry_run=False, client_factory=Client):
        self.settings = settings or Settings()
        self.dry_run = dry_run
        self._factory = client_factory
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._thread = None
        self._pending = None
        self._last_submission = -math.inf
        self._error = None
        self._message = "Connecting"

    def start(self):
        if self._thread is not None:
            raise RuntimeError("Gimbal already started")
        self._thread = threading.Thread(target=self._run, name="gimbal-ethernet", daemon=True)
        self._thread.start()
        if not self._ready.wait(3):
            self.close()
            raise TimeoutError("Gimbal connection timed out")
        self.check()
        return self

    def check(self):
        with self._lock:
            error, message = self._error, self._message
        if error is not None:
            raise RuntimeError(f"Gimbal stopped: {error}") from error
        return message

    def update(self, result, frame_received):
        self.check()
        timestamp = frame_received - self.settings.capture_delay_s
        if not math.isfinite(timestamp) or timestamp > time.perf_counter() + 0.001:
            raise ValueError("Invalid frame timestamp")
        angles = None if result is None else (result.pan_error_deg, result.tilt_error_deg)
        if angles is not None and not all(math.isfinite(v) for v in angles):
            raise ValueError("Nonfinite vision angles")
        with self._lock:
            if timestamp <= self._last_submission:
                raise ValueError("Frame timestamps must increase")
            self._last_submission = timestamp
            self._pending = (timestamp, angles)

    def close(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)

    def _message_set(self, text):
        with self._lock:
            self._message = text

    def _run(self):
        clients, histories, limits = [], [deque(maxlen=100), deque(maxlen=100)], []
        attempted = False
        s = self.settings
        try:
            # Short bounded requests; a lost reply ends this session rather than
            # reusing uncorrelated STATUS replies from a timed-out transaction.
            for port in (s.tilt_port, s.pan_port):
                clients.append(self._factory(s.host, port, timeout=0.05))
            statuses = [read_status(c, i) for i, c in enumerate(clients)]
            limits = [read_limits(c, i, s) for i, c in enumerate(clients)]
            for i, status in enumerate(statuses):
                if status["state"] != READY:
                    raise RuntimeError(f"{AXES[i]} must start READY (state=5); stop the previous run with S2")
            self._message_set("Waiting for fresh camera frames" if not self.dry_run else "DRY RUN: feedback only")
            self._ready.set()
            targets = None
            seen_active = [False, False]
            armed = False
            queued_at = last_frame = last_detection = None
            observation = None
            last_target_report = 0.0
            while not self._stop.is_set():
                tick = time.perf_counter()
                for i, client in enumerate(clients):
                    if attempted and not self.dry_run:
                        client.request("HEARTBEAT")
                    before = time.perf_counter()
                    status = read_status(client, i)
                    after = time.perf_counter()
                    statuses[i] = status
                    stamp = (before + after) / 2 - status["encoder_age_us"] / 1e6
                    histories[i].append((stamp, status["position_mdeg"] / 1000))
                    if status["state"] not in (READY, WAKING, RUNNING):
                        raise RuntimeError(f"{AXES[i]} unexpected state {status['state']}")
                    if seen_active[i] and status["state"] == READY:
                        raise RuntimeError(f"{AXES[i]} stopped; restart explicitly, no automatic rearm")
                    seen_active[i] |= status["state"] in (WAKING, RUNNING)
                current = tuple(st["position_mdeg"] / 1000 for st in statuses)
                now = time.perf_counter()
                with self._lock:
                    pending, self._pending = self._pending, None
                if pending is not None:
                    timestamp, angles = pending
                    if now - timestamp <= s.stale_s:
                        last_frame = timestamp
                        observation = pending
                        if angles is not None:
                            last_detection = timestamp
                fresh = observation is not None and now - observation[0] <= s.stale_s
                tracking = fresh and observation[1] is not None
                if not attempted and not self.dry_run and tracking:
                    if any(st["state"] != READY for st in statuses):
                        raise RuntimeError("An axis started before this session queued its targets; stop with S2")
                    # First queue holds the current pose, so S1 causes no deliberate
                    # jump. A just-zeroed axis at 0 is clamped to its 2-degree margin.
                    targets = tuple(min(hi, max(lo, q)) for q, (lo, hi) in zip(current, limits))
                    for i, (q, target, window) in enumerate(zip(current, targets, (s.tilt_limits, s.pan_limits))):
                        if q != target and (window is not None or abs(q - target) > 2):
                            raise RuntimeError(f"{AXES[i]} starting position {q:.2f} is outside tracking limits {limits[i]}")
                    attempted = True  # also disable on a partial send / lost ACK
                    for client, target in zip(clients, targets):
                        client.request("POSITION", round(target * 1000))
                    queued_at = now
                    print("Gimbal ready: starting positions queued. Press physical S1 once; S2/q stops.", flush=True)
                if attempted:
                    if last_frame is None or now - last_frame > s.stale_s:
                        raise RuntimeError("Camera frames are stale; stopping both axes")
                    if last_detection is None or now - last_detection > s.loss_stop_s:
                        raise RuntimeError("Ball lost for too long; stopping both axes")
                    if not armed and now - queued_at > s.arm_timeout_s:
                        raise RuntimeError("Timed out waiting for physical S1")
                    armed |= all(st["state"] == RUNNING for st in statuses)
                if self.dry_run or armed:
                    if tracking and pending is not None and observation is pending:
                        pose = tuple(pose_at(h, observation[0]) for h in histories)
                        if not all(q is not None for q in pose) and armed:
                            raise RuntimeError("Cannot associate camera frame with fresh encoder feedback")
                        if all(q is not None for q in pose):
                            new = target_angles(pose, current, *observation[1], s, limits)
                            if not self.dry_run:
                                for i, client in enumerate(clients):
                                    if abs(new[i] - targets[i]) >= s.min_update_deg:
                                        client.request("POSITION", round(new[i] * 1000))
                            targets = new if self.dry_run else tuple(
                                new[i] if abs(new[i] - targets[i]) >= s.min_update_deg else targets[i] for i in (0, 1))
                # laptop-bridge adds a reporting-only TARGET operation. Detect it
                # through sys=; pcb-bringup-h743 does not support that operation.
                if attempted and "sys" in statuses[0] and now - last_target_report >= 0.1:
                    clients[0].request("TARGET", int(tracking))
                    last_target_report = now
                mode = "DRY RUN" if self.dry_run else "TRACKING" if armed and tracking else "HOLD (ball lost)" if armed else "WAIT S1" if attempted else "WAIT BALL"
                target_text = "" if targets is None else f" -> tilt {targets[0]:.2f}, pan {targets[1]:.2f} deg"
                self._message_set(f"{mode}: tilt {current[0]:.2f}, pan {current[1]:.2f} deg{target_text}")
                self._stop.wait(max(0, 0.05 - (time.perf_counter() - tick)))
        except BaseException as error:
            with self._lock:
                self._error = error
        finally:
            if attempted:
                for client in clients:
                    try:
                        client.request("DISABLE")
                    except (OSError, RuntimeError, ValueError):
                        pass
            for client in clients:
                client.close()
            self._ready.set()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Read-only Ethernet check for both gimbal axes")
    parser.add_argument("--config", type=Path, default=CONFIG_PATH)
    args = parser.parse_args(argv)
    clients = []
    try:
        settings = Settings.load(args.config)
        for axis, port in enumerate((settings.tilt_port, settings.pan_port)):
            client = Client(settings.host, port)
            clients.append(client)
            status = read_status(client, axis)
            limits = read_limits(client, axis, settings)
            print(f"{AXES[axis]} {settings.host}:{port}: measured={status['position_mdeg']/1000:.3f} deg, "
                  f"state={status['state']}, limits={limits}, calibrated/zero OK")
        print("Both axes replied. No motion commands sent. Run python -m vision.run --ethernet")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Ethernet check failed: {error}\nCheck cable, laptop IPv4 192.168.10.1/24, and board readiness.\n")
    finally:
        for client in clients:
            client.close()


if __name__ == "__main__":
    main()
