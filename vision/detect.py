from pathlib import Path

import numpy as np
from ultralytics import YOLO
from ultralytics.cfg import DEFAULT_CFG_DICT

from .utils.timing import timed


DEFAULT_IMAGE_SIZE = 640


class Detect:
    def __init__(self, model: str, confidence: float = 0.15,
                 image_size: int = DEFAULT_IMAGE_SIZE, device: str | None = None,
                 half: bool = False, coreml_compute_units: str = "cpu-and-ne"):
        model_path = Path(model).expanduser()
        is_coreml = model_path.suffix == ".mlpackage"
        if not (model_path.is_dir() if is_coreml else model_path.is_file()):
            raise FileNotFoundError(f"YOLO model does not exist: {model_path}")
        if not 0.0 < confidence <= 1.0:
            raise ValueError("Confidence must be greater than 0 and at most 1.")
        if isinstance(image_size, bool) or not isinstance(image_size, int) or image_size <= 0:
            raise ValueError("Inference image size must be a positive integer.")
        self.model_name = str(model_path)
        self.coreml = None
        if is_coreml:
            from .coreml_detect import CoreMLDetector
            self.coreml = CoreMLDetector(model_path, image_size, coreml_compute_units)
            self.model = self.coreml
        else:
            self.model = YOLO(self.model_name)
        self.confidence = confidence
        self.image_size = image_size
        self.device = device
        self.half = half
        # New Ultralytics versions warn on every call using the deprecated half flag.
        self.precision_args = ({"quantize": 16 if half else 32} if "quantize" in DEFAULT_CFG_DICT
                               else {"half": half})
        self.inference_ms = 0.0
        names = self.model.names
        names = names.items() if isinstance(names, dict) else enumerate(names)
        self.ball_class_ids = [int(class_id) for class_id, name in names
                               if str(name).strip().lower() in {"sports ball", "ball"}]
        if not self.ball_class_ids:
            raise ValueError("The supplied model has no 'sports ball' or 'ball' class.")

    @property
    def actual_device(self) -> str | None:
        """Report the selected backend once inference has initialized it."""
        if self.coreml is not None:
            return self.coreml.actual_device
        predictor = getattr(self.model, "predictor", None)
        device = getattr(predictor, "device", None)
        return str(device) if device is not None else None

    def warmup(self, width: int, height: int) -> None:
        """Pay first-use compilation cost before capture and gimbal control start."""
        image = np.zeros((height, width, 3), dtype=np.uint8)
        for _ in range(3):
            self.detect(image)

    @timed("inference_ms")
    def detect(self, img) -> np.ndarray:
        """Return [x1, y1, x2, y2, confidence, class_id], or an empty array."""
        if isinstance(img, (str, Path)):
            image_path = Path(img).expanduser()
            if not image_path.is_file():
                raise FileNotFoundError(f"Image does not exist: {image_path}")
            img = str(image_path)
        elif not isinstance(img, np.ndarray) or img.ndim != 3 or img.shape[2] != 3 or not img.size:
            raise ValueError("Expected a nonempty OpenCV BGR image or a local image path.")

        if self.coreml is not None:
            return self.coreml.detect(img, self.ball_class_ids, self.confidence)
        results = self.model.predict(img, conf=self.confidence,
                                     classes=self.ball_class_ids, imgsz=self.image_size,
                                     rect=True, device=self.device, verbose=False, **self.precision_args)
        boxes = results[0].boxes
        if boxes is None or len(boxes) == 0:
            return np.array([], dtype=float)
        # One device-to-host transfer instead of three separately synchronized copies.
        rows = boxes.data.cpu().numpy()
        coordinates, confidence, classes = rows[:, :4], rows[:, -2], rows[:, -1]
        candidates = np.flatnonzero(np.isin(classes, self.ball_class_ids)
                                    & (confidence >= self.confidence))
        if candidates.size == 0:
            return np.array([], dtype=float)
        best = candidates[np.argmax(confidence[candidates])]
        return np.array([*coordinates[best], confidence[best], classes[best]], dtype=float)
