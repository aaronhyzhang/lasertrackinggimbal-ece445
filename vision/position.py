from dataclasses import dataclass
import math

import cv2 as cv
import numpy as np

from .utils.calibration import Calibration


BALL_DIAMETER_M = 0.24


@dataclass(frozen=True, slots=True)
class PositionResult:
    x_m: float | None
    y_m: float | None
    z_m: float | None
    zx_m: float | None
    zy_m: float | None
    range_m: float | None
    pan_error_rad: float
    tilt_error_rad: float
    pan_error_deg: float
    tilt_error_deg: float
    center_px: tuple[float, float]
    raw_center_px: tuple[float, float]
    width_px: float
    height_px: float
    confidence: float | None
    class_id: int | None
    clipped: bool
    depth_reliable: bool


class Position:
    """Camera coordinates: right +X, down +Y, forward +Z; tilt is positive up."""

    def __init__(self, calibration_path: str, ball_diameter_m: float = BALL_DIAMETER_M):
        if not math.isfinite(ball_diameter_m) or ball_diameter_m <= 0:
            raise ValueError("Ball diameter must be a finite positive number in meters.")
        
        self.calibration = Calibration.load(calibration_path)
        self.ball_diameter_m = ball_diameter_m

    def estimate(self, bbox: np.ndarray, image_size: tuple[int, int]) -> PositionResult:
        """Estimate camera-frame position from a known ball diameter.

        The frame must match the calibration's saved resolution. After lens
        correction, the center defines a viewing ray using focal lengths fx/fy
        and optical center cx/cy. Pan is positive right; tilt is positive up.

        Depth estimates are zx = fx * diameter / box_width and
        zy = fy * diameter / box_height, averaged to obtain forward depth z.
        Then x/y scale the viewing ray by z; range is sqrt(x*x + y*y + z*z).

        A box touching an image edge is considered clipped: its apparent size
        may be incomplete. Angles are still returned but all metric distances
        remain None (unavailable, not zero). depth_reliable only checks clipping;
        it does not guarantee detection or distance accuracy.
        """

        self.calibration.validate_image_size(image_size) 

        bbox = np.asarray(bbox, dtype=float).reshape(-1)
        if bbox.size < 4 or not np.all(np.isfinite(bbox)):
            raise ValueError("Bounding box must contain at least four finite coordinates.")
        
        x1, y1, x2, y2 = bbox[:4]
        if x2 <= x1 or y2 <= y1:
            raise ValueError("Bounding box width and height must both be positive.")
        
        width, height = image_size
        if x2 <= 0 or y2 <= 0 or x1 >= width or y1 >= height:
            raise ValueError("Bounding box lies outside the image.")

        # getting raw middle point from bboxes
        raw_u, raw_v = (x1 + x2) / 2, (y1 + y2) / 2
        points = np.array([[raw_u, raw_v], [x1, raw_v], [x2, raw_v],
                           [raw_u, y1], [raw_u, y2]], dtype=np.float64).reshape(-1, 1, 2)

        # undistorting image points based on calibration for bboxes
        matrix = self.calibration.camera_matrix
        corrected = cv.undistortPoints(points, cameraMatrix=matrix,
                                       distCoeffs=self.calibration.dist_coeffs,
                                       P=matrix).reshape(-1, 2)
        u, v = corrected[0]
        box_width = corrected[2, 0] - corrected[1, 0]
        box_height = corrected[4, 1] - corrected[3, 1]

        if not np.all(np.isfinite(corrected)) or box_width <= 0 or box_height <= 0:
            raise ValueError("Undistorted bounding box dimensions are invalid.")

        # perspective projection for real world position
        fx, fy = matrix[0, 0], matrix[1, 1]
        cx, cy = matrix[0, 2], matrix[1, 2]
        ray_x, ray_y = (u - cx) / fx, (v - cy) / fy
        pan = math.atan2(ray_x, 1.0)
        tilt = -math.atan2(ray_y, 1.0)
        clipped = x1 <= 0 or y1 <= 0 or x2 >= width - 1 or y2 >= height - 1
        zx = zy = x = y = z = distance = None
        if not clipped:
            zx = float(fx * self.ball_diameter_m / box_width)
            zy = float(fy * self.ball_diameter_m / box_height)
            z = (zx + zy) / 2
            x, y = float(z * ray_x), float(z * ray_y)
            distance = math.sqrt(x * x + y * y + z * z)
        return PositionResult(
            x_m=x, y_m=y, z_m=z, zx_m=zx, zy_m=zy, range_m=distance,
            pan_error_rad=pan, tilt_error_rad=tilt,
            pan_error_deg=math.degrees(pan), tilt_error_deg=math.degrees(tilt),
            center_px=(float(u), float(v)), raw_center_px=(float(raw_u), float(raw_v)),
            width_px=float(box_width), height_px=float(box_height),
            confidence=float(bbox[4]) if bbox.size > 4 else None,
            class_id=int(bbox[5]) if bbox.size > 5 else None,
            clipped=bool(clipped), depth_reliable=not bool(clipped),
        )
