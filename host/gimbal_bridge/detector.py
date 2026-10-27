"""Ball detectors. Each takes a BGR frame and returns the best Detection or None.

  color  Classical HSV threshold + contour circularity (design doc 2.4 fallback).
         Fast (a few ms at 640x480) and needs no model; tune `hsv_ranges` to the ball.
  yolo   Ultralytics YOLO, class "sports ball" by default. Needs `pip install ultralytics`
         and a model file; imported only when selected.

Add another detector by implementing `detect(frame) -> Detection | None`.
"""
from dataclasses import dataclass
import math

import cv2
import numpy as np


@dataclass
class Detection:
    cx: float      # ball centre, pixels
    cy: float
    w: float       # bounding box size, pixels
    h: float
    score: float   # 0..1


class ColorBallDetector:
    def __init__(self, hsv_ranges=(((20, 80, 80), (40, 255, 255)),), min_area=60, min_circularity=0.6, blur=5):
        """hsv_ranges: OpenCV HSV (H 0-179). Default: yellow/green ball. Several ranges are OR-ed
        (e.g. red wraps around H=0, so use two ranges)."""
        self.ranges = [(np.array(lo, np.uint8), np.array(hi, np.uint8)) for lo, hi in hsv_ranges]
        self.min_area = min_area
        self.min_circularity = min_circularity
        self.blur = blur
        self.kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))

    def mask(self, frame):
        if self.blur:
            frame = cv2.GaussianBlur(frame, (self.blur, self.blur), 0)
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        m = None
        for lo, hi in self.ranges:
            part = cv2.inRange(hsv, lo, hi)
            m = part if m is None else cv2.bitwise_or(m, part)
        m = cv2.morphologyEx(m, cv2.MORPH_OPEN, self.kernel)
        return cv2.morphologyEx(m, cv2.MORPH_CLOSE, self.kernel)

    def detect(self, frame):
        contours, _ = cv2.findContours(self.mask(frame), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        best = None
        for c in contours:
            area = cv2.contourArea(c)
            if area < self.min_area:
                continue
            perimeter = cv2.arcLength(c, True)
            circularity = 4 * math.pi * area / (perimeter * perimeter) if perimeter else 0
            if circularity < self.min_circularity:
                continue
            (x, y), r = cv2.minEnclosingCircle(c)
            fill = area / (math.pi * r * r) if r else 0
            score = min(1.0, circularity) * min(1.0, fill)
            # Prefer the largest round blob; score breaks near-ties.
            key = area * score
            if best is None or key > best[0]:
                m = cv2.moments(c)
                cx, cy = (m['m10'] / m['m00'], m['m01'] / m['m00']) if m['m00'] else (x, y)
                best = (key, Detection(cx, cy, 2 * r, 2 * r, score))
        return best[1] if best else None


class YoloDetector:
    def __init__(self, model='yolo11n.pt', classes=('sports ball',), conf=0.35, imgsz=640, device=None):
        try:
            from ultralytics import YOLO
        except ImportError as exc:
            raise RuntimeError('YOLO detector needs: py -3 -m pip install ultralytics') from exc
        self.model = YOLO(model)
        names = self.model.names
        self.class_ids = [i for i, n in names.items() if n in classes]
        if not self.class_ids:
            raise RuntimeError(f'model has none of the classes {classes}')
        self.conf, self.imgsz, self.device = conf, imgsz, device

    def detect(self, frame):
        result = self.model.predict(frame, conf=self.conf, imgsz=self.imgsz, classes=self.class_ids,
                                    device=self.device, verbose=False)[0]
        if not len(result.boxes):
            return None
        i = int(result.boxes.conf.argmax())
        x1, y1, x2, y2 = (float(v) for v in result.boxes.xyxy[i])
        return Detection((x1 + x2) / 2, (y1 + y2) / 2, x2 - x1, y2 - y1, float(result.boxes.conf[i]))


def make_detector(cfg):
    kind = cfg.get('type', 'color')
    if kind == 'color':
        ranges = cfg.get('hsv_ranges', [[[20, 80, 80], [40, 255, 255]]])
        return ColorBallDetector([tuple(map(tuple, r)) for r in ranges], cfg.get('min_area', 60),
                                 cfg.get('min_circularity', 0.6))
    if kind == 'yolo':
        return YoloDetector(cfg.get('model', 'yolo11n.pt'), tuple(cfg.get('classes', ['sports ball'])),
                            cfg.get('conf', 0.35), cfg.get('imgsz', 640), cfg.get('device'))
    raise ValueError(f'unknown detector type {kind!r}')
