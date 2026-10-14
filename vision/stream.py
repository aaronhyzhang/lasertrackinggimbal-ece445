"""Open a camera at the requested mode and return unmodified frames."""

import math

import cv2 as cv
import numpy as np


class Stream:
    def __init__(self, camera_path: str, width: int = 1920,
                 height: int = 1080, fps: float = 60):
        if width <= 0 or height <= 0 or not math.isfinite(fps) or fps <= 0:
            raise ValueError("Camera width, height, and FPS must be positive.")
        source = str(camera_path).strip()
        self.source = int(source) if source.isdecimal() else (source or 0)
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
        except Exception:
            self.release()
            raise

    def read(self) -> np.ndarray:
        ok, frame = self.capture.read()
        if not ok or frame is None or frame.size == 0:
            raise RuntimeError(f"No frame from {self.source!r}; camera disconnected or video ended.")
        return frame

    def release(self) -> None:
        self.capture.release()
