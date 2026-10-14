from dataclasses import FrozenInstanceError
import math
from unittest.mock import patch

import numpy as np

from vision.position import Position, PositionResult
from vision.unit_tests.helpers import VisionTest


class PositionTests(VisionTest):
    def setUp(self):
        super().setUp()
        self.position = Position(self.calibration_path)

    def test_centered_ball_has_known_depth_and_zero_angles(self):
        cx, cy = self.size[0] / 2, self.size[1] / 2
        result = self.position.estimate(np.array([cx-50, cy-50, cx+50, cy+50, .9, 32]), self.size)
        self.assertIsInstance(result, PositionResult)
        self.assertAlmostEqual(result.z_m, 2.4)
        self.assertAlmostEqual(result.range_m, 2.4)
        self.assertEqual((result.x_m, result.y_m, result.pan_error_deg, result.tilt_error_deg), (0, 0, 0, 0))
        self.assertEqual(result.confidence, .9)
        self.assertEqual(result.class_id, 32)
        self.assertTrue(result.depth_reliable)
        with self.assertRaises(FrozenInstanceError):
            result.z_m = 0

    def test_right_and_above_have_positive_pan_and_tilt(self):
        cx, cy = self.size[0] / 2, self.size[1] / 2
        result = self.position.estimate(np.array([cx+50, cy-150, cx+150, cy-50]), self.size)
        self.assertAlmostEqual(result.x_m, .24)
        self.assertAlmostEqual(result.y_m, -.24)
        self.assertAlmostEqual(result.pan_error_rad, math.atan(.1))
        self.assertAlmostEqual(result.tilt_error_rad, math.atan(.1))
        self.assertAlmostEqual(result.range_m, math.sqrt(2.4**2 + 2*.24**2))
        self.assertIsNone(result.confidence)

    def test_diameter_scales_depth_but_not_angles(self):
        box = np.array([500, 200, 600, 300])
        a = self.position.estimate(box, self.size)
        b = Position(self.calibration_path, .48).estimate(box, self.size)
        self.assertAlmostEqual(b.z_m, 2*a.z_m)
        self.assertEqual(a.pan_error_rad, b.pan_error_rad)

    def test_clipping_keeps_angles_but_suppresses_distance(self):
        for box in ([0, 100, 100, 200], [900, 100, 992, 200], [100, 0, 200, 100], [100, 500, 200, 558]):
            with self.subTest(box=box):
                result = self.position.estimate(np.array(box), self.size)
                self.assertTrue(result.clipped)
                self.assertFalse(result.depth_reliable)
                self.assertIsNone(result.z_m)
                self.assertIsNone(result.range_m)
                self.assertTrue(math.isfinite(result.pan_error_rad))

    def test_invalid_boxes_and_resolution_are_rejected(self):
        for box in ([], [1, 2, 3], [10, 10, 5, 20], [10, 20, 30, 20],
                    [np.nan, 0, 20, 30], [-20, 10, -1, 20], [1000, 10, 1010, 20]):
            with self.subTest(box=box), self.assertRaises(ValueError):
                self.position.estimate(np.array(box), self.size)
        with self.assertRaises(ValueError):
            self.position.estimate(np.array([10, 10, 20, 20]), (1920, 1080))

    def test_invalid_ball_diameters_are_rejected(self):
        for diameter in (0, -1, np.nan, np.inf):
            with self.subTest(diameter=diameter), self.assertRaises(ValueError):
                Position(self.calibration_path, diameter)

    def test_invalid_undistortion_is_rejected(self):
        with patch("vision.position.cv.undistortPoints", return_value=np.full((5, 1, 2), np.nan)):
            with self.assertRaisesRegex(ValueError, "Undistorted"):
                self.position.estimate(np.array([10, 10, 20, 20]), self.size)
