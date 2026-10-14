import csv
import math

import cv2 as cv
import numpy as np

from vision import run
from vision.detect import Detect
from vision.position import Position
from vision.utils.draw import draw_result, draw_text
from vision.unit_tests.helpers import IMAGE, MODEL, VISION, VisionTest, mock_gui


class ImageIntegrationTests(VisionTest):
    @classmethod
    def setUpClass(cls):
        # Real, local model on CPU: no GPU requirement and no weight downloads.
        cls.detector = Detect(str(MODEL), confidence=.15, device="cpu")

    def test_real_yolo_detection_position_and_drawing_on_test2(self):
        box = self.detector.detect(self.frame)
        self.assertEqual(box.shape, (6,))
        x1, y1, x2, y2, confidence, class_id = box
        self.assertEqual(class_id, 32)
        self.assertGreater(confidence, .15)
        self.assertTrue(0 <= x1 < x2 <= self.size[0])
        self.assertTrue(0 <= y1 < y2 <= self.size[1])
        # Broad hand-checked ball region; do not pin model-version-specific decimals.
        self.assertTrue(480 < (x1+x2)/2 < 600)
        self.assertTrue(430 < (y1+y2)/2 < 550)
        result = Position(self.calibration_path).estimate(box, self.size)
        self.assertTrue(result.depth_reliable)
        self.assertTrue(math.isfinite(result.range_m))
        self.assertGreater(result.z_m, 0)
        original = self.frame.copy()
        draw_text(self.frame, draw_result(self.frame, box, result))
        self.assertFalse(np.array_equal(original, self.frame))
        self.assertEqual(self.detector.actual_device, "cpu")

    def test_real_run_main_with_image_and_csv(self):
        mock_gui(self.stack, (0, ord("q")))
        log_path = self.directory / "image.csv"
        run.main(["--image", str(IMAGE), "--calibration", str(self.calibration_path),
                  "--model", str(MODEL), "--device", "cpu", "--log-csv", str(log_path)])
        with log_path.open(newline="", encoding="utf-8") as saved:
            row, = list(csv.DictReader(saved))
        self.assertEqual(row["detected"], "True")
        self.assertEqual(row["device_actual"], "cpu")
        self.assertGreater(float(row["z_m"]), 0)
        self.assertEqual(float(row["capture_ms"]), 0.)
        cv.imshow.assert_called_once()
        self.assertEqual(cv.waitKey.call_count, 2)

    def test_real_camera_calibration_rejects_unrelated_test2_resolution(self):
        position = Position(VISION / "calibration/camera_calibration.npz")
        self.assertNotEqual(position.calibration.image_size, self.size)
        with self.assertRaisesRegex(ValueError, "differs from calibration"):
            position.estimate(np.array([493, 452, 576, 530]), self.size)
