"""Per-frame CSV measurements; existing files are never overwritten."""

import csv
from dataclasses import dataclass, fields
from pathlib import Path
import time

import numpy as np

from ..position import PositionResult


CSV_FIELDS = (
    "frame_index", "elapsed_s", "detected", "geometry_error", "ball_diameter_m", "imgsz",
    "device_requested", "device_actual", "capture_ms", "inference_ms", "display_ms", "loop_ms", "fps",
    "x1_px", "y1_px", "x2_px", "y2_px",
    "confidence", "class_id", "x_m", "y_m", "z_m", "zx_m", "zy_m", "range_m",
    "pan_error_rad", "tilt_error_rad", "pan_error_deg", "tilt_error_deg",
    "center_u_px", "center_v_px", "raw_center_u_px", "raw_center_v_px",
    "width_px", "height_px", "clipped", "depth_reliable",
)
POSITION_FIELDS = tuple(field.name for field in fields(PositionResult) if field.name in CSV_FIELDS)


@dataclass(slots=True)
class Measurement:
    frame_index: int
    elapsed_s: float
    bbox: np.ndarray
    result: PositionResult | None
    geometry_error: str | None
    inference_ms: float
    loop_ms: float
    fps: float
    ball_diameter_m: float
    imgsz: int
    capture_ms: float
    display_ms: float
    device_requested: str | None
    device_actual: str | None


MEASUREMENT_FIELDS = tuple(field.name for field in fields(Measurement)
                           if field.name not in ("bbox", "result"))


class MeasurementLogger:
    def __init__(self, path: str):
        path = Path(path).expanduser()
        path.parent.mkdir(parents=True, exist_ok=True)
        self.file = path.open("x", newline="", encoding="utf-8")
        try:
            self.writer = csv.DictWriter(self.file, fieldnames=CSV_FIELDS)
            self.writer.writeheader()
        except Exception:
            self.file.close()
            raise
        self.last_flush = 0.0
        print(f"CSV: {path.resolve()}")

    def log(self, measurement: Measurement, flush: bool = False) -> None:
        row = {name: getattr(measurement, name) for name in MEASUREMENT_FIELDS}
        row["detected"] = bool(measurement.bbox.size)
        row["device_requested"] = measurement.device_requested or "auto"
        if measurement.bbox.size:
            row.update(zip(("x1_px", "y1_px", "x2_px", "y2_px", "confidence", "class_id"),
                           measurement.bbox))
        result = measurement.result
        if result is not None:
            row.update((name, getattr(result, name)) for name in POSITION_FIELDS)
            row["center_u_px"], row["center_v_px"] = result.center_px
            row["raw_center_u_px"], row["raw_center_v_px"] = result.raw_center_px
        self.writer.writerow(row)
        now = time.perf_counter()
        if flush or now - self.last_flush >= 1:
            self.file.flush()
            self.last_flush = now

    def close(self) -> None:
        self.file.close()
