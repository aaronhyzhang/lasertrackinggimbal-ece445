"""Open a camera at the requested mode and return unmodified frames."""

import math
import threading
import time

import cv2 as cv
import numpy as np


class Stream:
    def __init__(self, camera_path: str, width: int = 1920,
                 height: int = 1080, fps: float = 60, latest: bool = False):
        if width <= 0 or height <= 0 or not math.isfinite(fps) or fps <= 0:
            raise ValueError("Camera width, height, and FPS must be positive.")
        source = str(camera_path).strip()
        self.source = int(source) if source.isdecimal() else (source or 0)
        self.received_at = None
        self.capture_fps = 0.0
        self.dropped_frames = 0
        self._thread = None
        self._condition = threading.Condition()
        self._stop = threading.Event()
        self._packet = None
        self._error = None
        self._sequence = self._consumed = 0
        self.capture = cv.VideoCapture(self.source)
        if not self.capture.isOpened():
            self.release()
            raise RuntimeError(f"Could not open camera/video {self.source!r}. Check the camera index and permissions.")
        try:
            if isinstance(self.source, int) or source.startswith("/dev/video"):
                # Keep bandwidth and buffering requests for high-FPS camera capture.
                for name, prop, value in (
                    ("MJPEG", cv.CAP_PROP_FOURCC, cv.VideoWriter_fourcc(*"MJPG")),
                    ("width", cv.CAP_PROP_FRAME_WIDTH, width),
                    ("height", cv.CAP_PROP_FRAME_HEIGHT, height),
                    ("FPS", cv.CAP_PROP_FPS, fps),
                    ("buffer size", cv.CAP_PROP_BUFFERSIZE, 1),
                ):
                    try:
                        accepted = self.capture.set(prop, value)
                    except cv.error:
                        accepted = False
                    if not accepted:
                        print(f"Camera did not accept {name}; using the driver setting.")
            self.mode = {"backend": self.capture.getBackendName(),
                         "width": self.capture.get(cv.CAP_PROP_FRAME_WIDTH),
                         "height": self.capture.get(cv.CAP_PROP_FRAME_HEIGHT),
                         "reported_fps": self.capture.get(cv.CAP_PROP_FPS)}
            print(f"Camera negotiated: {self.mode}")
            if latest:
                if not isinstance(self.source, int):
                    raise ValueError("Latest-frame capture requires a numeric live camera index.")
                self._thread = threading.Thread(target=self._capture_latest, name="camera-capture", daemon=True)
                self._thread.start()
        except Exception:
            self.release()
            raise

    def _read_frame(self) -> np.ndarray:
        ok, frame = self.capture.read()
        if not ok or frame is None or frame.size == 0:
            raise RuntimeError(f"No frame from {self.source!r}; camera disconnected or video ended.")
        return frame

    def _capture_latest(self):
        previous = None
        try:
            while not self._stop.is_set():
                frame = self._read_frame()
                received = time.perf_counter()
                with self._condition:
                    if self._sequence > self._consumed:
                        self.dropped_frames += 1
                    self._sequence += 1
                    self._packet = (frame, received)
                    if previous is not None:
                        fps = 1 / max(received - previous, 1e-9)
                        self.capture_fps = fps if not self.capture_fps else .9 * self.capture_fps + .1 * fps
                    previous = received
                    self._condition.notify_all()
        except Exception as error:
            with self._condition:
                self._error = error
                self._condition.notify_all()
        finally:
            # Only the reader thread releases its capture, avoiding read/release races.
            self.capture.release()

    def read(self) -> np.ndarray:
        if self._thread is None:
            frame = self._read_frame()
            self.received_at = time.perf_counter()
            return frame
        with self._condition:
            ready = self._condition.wait_for(
                lambda: self._sequence > self._consumed or self._error is not None or self._stop.is_set(),
                timeout=2.0)
            if self._error is not None:
                raise RuntimeError(f"Camera capture failed: {self._error}") from self._error
            if not ready or self._stop.is_set():
                raise RuntimeError("Camera capture stopped or timed out.")
            self._consumed = self._sequence
            frame, self.received_at = self._packet
            return frame

    def release(self) -> None:
        if self._thread is None:
            self.capture.release()
            return
        self._stop.set()
        with self._condition:
            self._condition.notify_all()
        self._thread.join(timeout=2.0)
        if self._thread.is_alive():
            raise RuntimeError("Camera driver did not stop within two seconds.")
