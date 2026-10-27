"""Camera -> detector -> angles -> bridge, on its own thread.

Each new frame: detect the ball, convert its centre to angular error with the camera
intrinsics, add the fixed aim offset, and submit an Observation stamped with the frame's
capture time. No detection submits Observation(False) so the bridge holds (SEARCHING).
Timing statistics cover the laptop side of the design doc latency budget.
"""
from collections import deque
import threading
import time

from .tracking import Observation


class VisionLoop:
    def __init__(self, source, detector, intrinsics, bridge, aim_offset_deg=(0.0, 0.0), on_frame=None):
        if (source.width, source.height) != (intrinsics.width, intrinsics.height):
            intrinsics = intrinsics.scaled(source.width, source.height)
        self.source, self.detector, self.k, self.bridge = source, detector, intrinsics, bridge
        self.pan_offset, self.tilt_offset = aim_offset_deg
        self.on_frame = on_frame       # optional preview callback(frame, detection, observation)
        self.frames = self.detections = 0
        self.detect_ms = deque(maxlen=300)   # detector time per frame
        self.age_ms = deque(maxlen=300)      # capture -> submit (laptop pipeline latency)
        self.frame_times = deque(maxlen=60)
        self.last = None
        self.error = None
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, name='vision', daemon=True)
        self._thread.start()
        return self

    def _run(self):
        seq = 0
        try:
            while not self._stop.is_set():
                got = self.source.read(seq, timeout=0.5)
                if got is None:
                    continue
                frame, t_capture, seq = got
                start = time.perf_counter()
                det = self.detector.detect(frame)
                self.detect_ms.append((time.perf_counter() - start) * 1000)
                if det is None:
                    obs = Observation(False, t_capture=t_capture)
                else:
                    pan, tilt = self.k.angles(det.cx, det.cy)
                    obs = Observation(True, pan + self.pan_offset, tilt + self.tilt_offset, t_capture)
                    self.detections += 1
                self.bridge.submit(obs)
                self.age_ms.append((time.monotonic() - t_capture) * 1000)
                self.frames += 1
                self.frame_times.append(t_capture)
                self.last = (det, obs)
                if self.on_frame:
                    self.on_frame(frame, det, obs)
        except BaseException as exc:  # noqa: BLE001 - reported by the launcher, which then stops
            self.error = exc

    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)

    def stats(self):
        fps = 0.0
        if len(self.frame_times) > 1:
            span = self.frame_times[-1] - self.frame_times[0]
            fps = (len(self.frame_times) - 1) / span if span > 0 else 0.0
        avg = lambda q: sum(q) / len(q) if q else 0.0  # noqa: E731
        return dict(fps=fps, frames=self.frames, detection_rate=self.detections / self.frames if self.frames else 0.0,
                    detect_ms=avg(self.detect_ms), pipeline_ms=avg(self.age_ms),
                    pipeline_ms_max=max(self.age_ms) if self.age_ms else 0.0)
