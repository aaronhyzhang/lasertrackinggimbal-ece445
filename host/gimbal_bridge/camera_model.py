"""Camera intrinsics and pixel -> angle conversion (design doc 2.2.5).

Intrinsics come from `camcal` (OpenCV checkerboard calibration), saved as JSON:
    {"width": 640, "height": 480, "fx": ..., "fy": ..., "cx": ..., "cy": ...,
     "dist": [k1, k2, p1, p2, k3], "rms": 0.31}
Without a calibration, `from_fov()` gives a pinhole estimate from the horizontal
field of view; good enough to test signs, not for accurate aiming.
"""
from dataclasses import dataclass, field, asdict
import json
import math


@dataclass
class Intrinsics:
    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    dist: list = field(default_factory=list)  # OpenCV order k1, k2, p1, p2[, k3...]
    rms: float = None                          # reprojection error from calibration, pixels

    @classmethod
    def from_fov(cls, width, height, hfov_deg):
        f = (width / 2) / math.tan(math.radians(hfov_deg) / 2)
        return cls(width, height, f, f, width / 2, height / 2)

    @classmethod
    def load(cls, path):
        with open(path, encoding='utf-8') as fh:
            data = json.load(fh)
        return cls(**{k: data[k] for k in ('width', 'height', 'fx', 'fy', 'cx', 'cy')},
                   dist=list(data.get('dist', [])), rms=data.get('rms'))

    def save(self, path):
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(asdict(self), fh, indent=2)
            fh.write('\n')

    def scaled(self, width, height):
        """Same camera at another capture resolution (same aspect and sensor crop)."""
        sx, sy = width / self.width, height / self.height
        return Intrinsics(width, height, self.fx * sx, self.fy * sy, self.cx * sx, self.cy * sy, list(self.dist), self.rms)

    def undistort(self, u, v):
        """Pixel -> ideal (distortion-free) normalized coordinates (x, y), y down."""
        x, y = (u - self.cx) / self.fx, (v - self.cy) / self.fy
        if any(self.dist):
            import cv2
            import numpy as np
            k = np.array([[self.fx, 0, self.cx], [0, self.fy, self.cy], [0, 0, 1]], dtype=np.float64)
            out = cv2.undistortPoints(np.array([[[u, v]]], dtype=np.float64), k, np.array(self.dist, dtype=np.float64))
            x, y = float(out[0, 0, 0]), float(out[0, 0, 1])
        return x, y

    def angles(self, u, v):
        """Ball pixel -> (pan_error_deg, tilt_error_deg); right of centre and above are positive."""
        x, y = self.undistort(u, v)
        return math.degrees(math.atan(x)), math.degrees(math.atan(-y))

    def project(self, pan_error_deg, tilt_error_deg):
        """Inverse of angles() for an undistorted camera; used by the synthetic camera."""
        return (self.cx + self.fx * math.tan(math.radians(pan_error_deg)),
                self.cy - self.fy * math.tan(math.radians(tilt_error_deg)))
