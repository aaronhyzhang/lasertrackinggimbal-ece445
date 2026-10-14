from pathlib import Path

import numpy as np
from ultralytics import YOLO

from .utils.timing import timed


DEFAULT_IMAGE_SIZE = 640


class Detect:
    def __init__(self, model: str, confidence: float = 0.15,
                 image_size: int = DEFAULT_IMAGE_SIZE, device: str | None = None):
        model_path = Path(model).expanduser()
        if not model_path.is_file():
            raise FileNotFoundError(f"YOLO model does not exist: {model_path}")
        if not 0.0 < confidence <= 1.0:
            raise ValueError("Confidence must be greater than 0 and at most 1.")
        if isinstance(image_size, bool) or not isinstance(image_size, int) or image_size <= 0:
            raise ValueError("Inference image size must be a positive integer.")
        self.model_name = str(model_path)
        self.model = YOLO(self.model_name)
        self.confidence = confidence
        self.image_size = image_size
        self.device = device
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
        predictor = getattr(self.model, "predictor", None)
        device = getattr(predictor, "device", None)
        return str(device) if device is not None else None

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

        results = self.model.predict(img, conf=self.confidence,
                                     classes=self.ball_class_ids, imgsz=self.image_size,
                                     rect=True, device=self.device, verbose=False)
        boxes = results[0].boxes
        if boxes is None or len(boxes) == 0:
            return np.array([], dtype=float)
        coordinates = boxes.xyxy.cpu().numpy()
        confidence = boxes.conf.cpu().numpy()
        classes = boxes.cls.cpu().numpy()
        candidates = np.flatnonzero(np.isin(classes, self.ball_class_ids)
                                    & (confidence >= self.confidence))
        if candidates.size == 0:
            return np.array([], dtype=float)
        best = candidates[np.argmax(confidence[candidates])]
        return np.array([*coordinates[best], confidence[best], classes[best]], dtype=float)
