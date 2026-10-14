from contextlib import ExitStack, redirect_stdout
import io
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import cv2 as cv
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
VISION = ROOT / "vision"
IMAGE = VISION / "media/test2.jpg"
MODEL = VISION / "weights/yolo26s.pt"


def write_calibration(directory, size=(992, 558)):
    """Synthetic intrinsics for logic tests, NOT a measured camera calibration."""
    width, height = size
    path = Path(directory) / "camera.npz"
    np.savez(path, camera_matrix=np.array([[1000., 0, width / 2],
                                         [0, 1000., height / 2], [0, 0, 1]]),
             dist_coeffs=np.zeros(5), image_size=np.array(size), rms_error=0.)
    return path


def mock_gui(stack, keys=(ord("q"),)):
    for name in ("namedWindow", "resizeWindow", "imshow", "destroyAllWindows"):
        stack.enter_context(patch.object(cv, name))
    stack.enter_context(patch.object(cv, "getWindowProperty", return_value=1))
    return stack.enter_context(patch.object(cv, "waitKey", side_effect=keys))


class VisionTest(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.directory = Path(self.stack.enter_context(TemporaryDirectory()))
        self.output = io.StringIO()
        self.stack.enter_context(redirect_stdout(self.output))
        self.frame = cv.imread(str(IMAGE))
        self.assertIsNotNone(self.frame, f"Missing or unreadable fixture: {IMAGE}")
        self.size = (self.frame.shape[1], self.frame.shape[0])
        self.calibration_path = write_calibration(self.directory, self.size)
