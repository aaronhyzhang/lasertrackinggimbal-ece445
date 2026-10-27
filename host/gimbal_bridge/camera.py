"""Frame sources with capture timestamps.

CameraSource     USB camera (or video file) read on its own thread, so the newest frame
                 is always available and old frames are dropped instead of queued.
SyntheticCamera  Renders a ball whose image position follows the real gimbal angles, so
                 the full detector -> bridge -> board loop can run without a camera.
"""
import math
import sys
import threading
import time

import cv2
import numpy as np


class CameraSource:
    def __init__(self, source=0, width=640, height=480, fps=30):
        backend = cv2.CAP_DSHOW if sys.platform == 'win32' and isinstance(source, int) else cv2.CAP_ANY
        self.cap = cv2.VideoCapture(source, backend)
        if not self.cap.isOpened():
            raise RuntimeError(f'cannot open camera/video {source!r}')
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self._frame, self._t, self._seq = None, 0.0, 0
        self._cond = threading.Condition()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name='camera', daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            ok, frame = self.cap.read()
            t = time.monotonic()  # grab time; USB transfer delay is not included
            if not ok:
                time.sleep(0.01)
                continue
            with self._cond:
                self._frame, self._t, self._seq = frame, t, self._seq + 1
                self._cond.notify_all()

    def read(self, after_seq=0, timeout=1.0):
        """Wait for a frame newer than after_seq; returns (frame, t_capture, seq) or None."""
        with self._cond:
            if not self._cond.wait_for(lambda: self._seq > after_seq or self._stop.is_set(), timeout):
                return None
            return (self._frame, self._t, self._seq) if self._seq > after_seq else None

    def close(self):
        self._stop.set()
        with self._cond:
            self._cond.notify_all()
        self._thread.join(timeout=2)
        self.cap.release()


class SyntheticCamera:
    """Ball moving in gimbal coordinates; the view follows `positions()` (tilt_deg, pan_deg).

    Uses the same sign convention as tracking.py, so a correct configuration converges."""

    def __init__(self, intrinsics, positions, signs=(1, -1), amplitude_deg=10.0, period_s=8.0, fps=30,
                 radius_px=14, color_bgr=(0, 230, 230), dropout=0.0, centre=None):
        self.k, self.positions, self.signs = intrinsics, positions, signs
        self.width, self.height = intrinsics.width, intrinsics.height
        self.amplitude, self.period, self.fps = amplitude_deg, period_s, fps
        self.radius, self.color, self.dropout = radius_px, color_bgr, dropout
        self.centre = centre
        self._t0 = time.monotonic()
        self._seq = 0
        self._rng = np.random.default_rng(1)

    def ball(self, t):
        if self.centre is None:
            self.centre = self.positions()
        tilt0, pan0 = self.centre
        return (tilt0 + self.amplitude / 2 * math.sin(4 * math.pi * t / self.period),
                pan0 + self.amplitude * math.sin(2 * math.pi * t / self.period))

    def read(self, after_seq=0, timeout=1.0):
        time.sleep(1 / self.fps)
        t = time.monotonic()
        frame = np.full((self.height, self.width, 3), (40, 40, 40), np.uint8)
        if self._rng.random() >= self.dropout:
            ball_tilt, ball_pan = self.ball(t - self._t0)
            tilt, pan = self.positions()
            err_tilt, err_pan = (ball_tilt - tilt) * self.signs[0], (ball_pan - pan) * self.signs[1]
            if abs(err_pan) < 80 and abs(err_tilt) < 80:
                u, v = self.k.project(err_pan, err_tilt)
                cv2.circle(frame, (int(round(u)), int(round(v))), self.radius, self.color, -1, cv2.LINE_AA)
        self._seq += 1
        return frame, t, self._seq

    def close(self):
        pass
