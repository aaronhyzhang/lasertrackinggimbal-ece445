"""Tracking preview annotations."""

import cv2 as cv
import numpy as np

from ..position import PositionResult


def draw_result(frame: np.ndarray, bbox: np.ndarray, result: PositionResult | None) -> list[str]:
    """The preview is raw; magenta is a coordinate in the corrected image plane."""
    if bbox.size == 0:
        return ["No ball detected"]
    x1, y1, x2, y2 = (round(value) for value in bbox[:4])
    cv.rectangle(frame, (x1, y1), (x2, y2), (0, 220, 0), 2)
    cv.circle(frame, (round((x1 + x2) / 2), round((y1 + y2) / 2)), 5, (0, 220, 0), -1)
    lines = [f"Ball confidence: {bbox[4]:.2f}"]
    if result is None:
        return lines
    corrected_center = tuple(round(value) for value in result.center_px)
    if 0 <= corrected_center[0] < frame.shape[1] and 0 <= corrected_center[1] < frame.shape[0]:
        cv.drawMarker(frame, corrected_center, (255, 0, 255), cv.MARKER_CROSS, 18, 2)
    lines.append(f"Pan error: {result.pan_error_deg:+.2f} deg   "
                 f"Tilt error: {result.tilt_error_deg:+.2f} deg")
    if result.depth_reliable:
        lines.append(f"Forward depth Z: {result.z_m:.3f} m | "
                     f"Camera-to-center range: {result.range_m:.3f} m")
        lines.append(f"Approx X/Y/Z: {result.x_m:+.3f}, {result.y_m:+.3f}, "
                     f"{result.z_m:.3f} m")
    else:
        lines.append("Depth unavailable: ball box touches image edge; angles are approximate")
    lines.append("Green: raw center | Magenta: corrected pixel coordinate on raw preview")
    return lines


def draw_text(frame: np.ndarray, lines: list[str]) -> None:
    for index, line in enumerate(lines):
        point = (15, 30 + index * 28)
        cv.putText(frame, line, point, cv.FONT_HERSHEY_SIMPLEX, 0.65, (0, 0, 255), 2, cv.LINE_AA)
