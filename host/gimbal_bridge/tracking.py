"""Vision error -> absolute axis target. Pure math, no networking.

Sign convention for vision errors (what the camera sees):
    pan_error_deg  > 0  ball is RIGHT of the image centre
    tilt_error_deg > 0  ball is ABOVE the image centre

The firmware's pan coordinate is 0 at the clockwise hard stop and increases
counter-clockwise, so turning toward a ball on the right usually DECREASES the
pan position (pan_sign = -1). Tilt direction depends on how J3 is mounted.
Both signs MUST be confirmed on the bench with the first small move.
"""
from dataclasses import dataclass
import math


@dataclass
class Observation:
    detected: bool
    pan_error_deg: float = 0.0
    tilt_error_deg: float = 0.0
    t_capture: float = None  # time.monotonic() at frame capture, if known


@dataclass
class TrackerConfig:
    pan_sign: int = -1
    tilt_sign: int = 1
    gain: float = 0.8            # fraction of the measured error corrected per frame
    deadband_deg: float = 0.5    # ignore errors smaller than this (encoder noise, detector jitter)
    max_step_deg: float = 20.0   # largest correction from one frame
    min_update_deg: float = 0.1  # smaller target changes are sent as heartbeats instead
    # Firmware accepts tilt 2..358 and pan 2..337; these defaults match Colin's tested sweeps.
    # Narrow tilt to the real mechanical/safe window before running on the assembled gimbal.
    tilt_limits: tuple = (15.0, 345.0)
    pan_limits: tuple = (15.0, 320.0)
    stale_s: float = 0.3         # observation older than this counts as "target lost"

    def validate(self):
        if self.pan_sign not in (-1, 1) or self.tilt_sign not in (-1, 1):
            raise ValueError('signs must be +1 or -1')
        if not 0 < self.gain <= 1.5:
            raise ValueError('gain must be in (0, 1.5]')
        for name, (low, high), firmware_max in (('tilt', self.tilt_limits, 358.0),
                                                 ('pan', self.pan_limits, 337.0)):
            if not 2.0 <= low < high <= firmware_max:
                raise ValueError(f'{name} limits must satisfy 2 <= min < max <= {firmware_max}')
        if self.deadband_deg < 0 or self.max_step_deg <= 0 or self.min_update_deg < 0:
            raise ValueError('deadband/min_update must be >= 0 and max_step > 0')


def pixel_to_angles(u, v, fx, fy, cx, cy):
    """Ball centre pixel (u, v) -> (pan_error_deg, tilt_error_deg) using camera intrinsics.

    Image v grows downward, so a ball above centre (v < cy) gives positive tilt error.
    """
    return math.degrees(math.atan2(u - cx, fx)), math.degrees(math.atan2(cy - v, fy))


def clamp(value, limits):
    return min(max(value, limits[0]), limits[1])


def next_target(measured_deg, error_deg, sign, cfg, limits, previous_target):
    """Return the new absolute target for one axis.

    The error was measured relative to where the camera pointed when the frame was
    taken, so the correction is applied to the measured encoder position, not to the
    previous target. Inside the deadband the previous target is held.
    """
    if not math.isfinite(error_deg) or abs(error_deg) < cfg.deadband_deg:
        return previous_target
    step = sign * cfg.gain * error_deg
    step = max(-cfg.max_step_deg, min(cfg.max_step_deg, step))
    return clamp(measured_deg + step, limits)
