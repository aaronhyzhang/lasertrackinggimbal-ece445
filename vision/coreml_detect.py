"""Direct Core ML inference for static, NMS-free YOLO26 detection exports.

Keep images as uint8 all the way to Core ML instead of going through the
Ultralytics float tensor -> NumPy -> uint8 -> PIL round trip on every frame.
"""

import ast
from pathlib import Path

import cv2 as cv
import numpy as np
from PIL import Image


def letterbox(image, size):
    """Return an RGB PIL image and the scale/padding needed to undo letterboxing."""
    height, width = size
    source_h, source_w = image.shape[:2]
    scale = min(width / source_w, height / source_h)
    resized_w, resized_h = round(source_w * scale), round(source_h * scale)
    left = round((width - resized_w) / 2 - 0.1)
    top = round((height - resized_h) / 2 - 0.1)
    resized = cv.resize(image, (resized_w, resized_h), interpolation=cv.INTER_LINEAR)
    padded = cv.copyMakeBorder(resized, top, height - resized_h - top,
                              left, width - resized_w - left, cv.BORDER_CONSTANT,
                              value=(114, 114, 114))
    return Image.fromarray(cv.cvtColor(padded, cv.COLOR_BGR2RGB)), scale, (left, top)


def select_ball(predictions, class_ids, confidence, scale, padding, original_shape):
    """Decode the NMS-free [1, N, 6] output back to original camera pixels."""
    predictions = np.asarray(predictions)
    if predictions.ndim != 3 or predictions.shape[0] != 1 or predictions.shape[2] != 6:
        raise ValueError("Expected NMS-free YOLO26 output [1, N, 6]; re-export with nms=False.")
    rows = predictions[0]
    valid = (np.isfinite(rows).all(axis=1) & (rows[:, 4] >= confidence)
             & np.isin(rows[:, 5], class_ids))
    rows = rows[valid]
    if not len(rows):
        return np.array([], dtype=float)
    best = rows[np.argmax(rows[:, 4])].astype(float, copy=True)
    best[:4] = (best[:4] - np.array([*padding, *padding])) / scale
    height, width = original_shape[:2]
    best[[0, 2]] = np.clip(best[[0, 2]], 0, width)
    best[[1, 3]] = np.clip(best[[1, 3]], 0, height)
    if best[2] <= best[0] or best[3] <= best[1]:
        return np.array([], dtype=float)
    return best


class CoreMLDetector:
    def __init__(self, path: Path, image_size: int, compute_units: str):
        import coremltools as ct

        units = {"cpu-and-ne": ct.ComputeUnit.CPU_AND_NE, "cpu-only": ct.ComputeUnit.CPU_ONLY}
        if compute_units not in units:
            raise ValueError("Core ML compute units must be cpu-and-ne or cpu-only.")
        # Do not silently fall back to CPU and report it as Neural Engine inference.
        self.model = ct.models.MLModel(str(path), compute_units=units[compute_units])
        spec = self.model.get_spec()
        metadata = self.model.user_defined_metadata
        if metadata.get("task") != "detect" or metadata.get("end2end") != "True":
            raise ValueError("Use a YOLO26 detection export with nms=False (end2end=True).")
        if len(spec.description.input) != 1 or len(spec.description.output) != 1:
            raise ValueError("Expected one image input and one NMS-free output.")
        input_spec = spec.description.input[0]
        image_type = input_spec.type.imageType
        if not input_spec.type.HasField("imageType") or not image_type.width or not image_type.height:
            raise ValueError("Use a static Core ML image export (dynamic=False, batch=1).")
        if image_type.colorSpace != ct.proto.FeatureTypes_pb2.ImageFeatureType.RGB:
            raise ValueError("Expected an RGB Core ML image input.")
        self.input_size = (image_type.height, image_type.width)
        if max(self.input_size) != image_size:
            raise ValueError(f"Export has fixed input {self.input_size}; use --imgsz {max(self.input_size)}.")
        self.input_name = input_spec.name
        self.output_name = spec.description.output[0].name
        self.names = ast.literal_eval(metadata["names"])
        # This describes allowed hardware, not proof of placement of every op.
        self.actual_device = f"coreml:{compute_units}"

    def detect(self, image, class_ids, confidence):
        if isinstance(image, (str, Path)):
            image = cv.imread(str(image))
            if image is None:
                raise ValueError("Cannot decode input image.")
        if image.dtype != np.uint8:
            raise ValueError("Core ML expects a uint8 BGR image.")
        prepared, scale, padding = letterbox(image, self.input_size)
        outputs = self.model.predict({self.input_name: prepared})
        return select_ball(outputs[self.output_name], class_ids, confidence,
                           scale, padding, image.shape)
