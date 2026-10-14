from dataclasses import replace

import numpy as np

from vision.position import Position
from vision.utils.draw import draw_result, draw_text
from vision.unit_tests.helpers import VisionTest


class DrawTests(VisionTest):
    def test_no_ball_leaves_image_unchanged(self):
        original = self.frame.copy()
        self.assertEqual(draw_result(self.frame, np.array([]), None), ["No ball detected"])
        np.testing.assert_array_equal(self.frame, original)

    def test_box_without_geometry_still_draws_confidence(self):
        original = self.frame.copy()
        lines = draw_result(self.frame, np.array([100, 100, 200, 200, .8, 32]), None)
        self.assertEqual(lines, ["Ball confidence: 0.80"])
        self.assertFalse(np.array_equal(original, self.frame))

    def test_valid_result_draws_center_and_position(self):
        box = np.array([100, 100, 200, 200, .8, 32])
        result = Position(self.calibration_path).estimate(box, self.size)
        lines = draw_result(self.frame, box, result)
        self.assertTrue(any("Forward depth Z" in line for line in lines))
        self.assertTrue(any("Pan error" in line for line in lines))
        u, v = map(round, result.center_px)
        np.testing.assert_array_equal(self.frame[v, u], [255, 0, 255])
        # Off-image corrected centers must not cause an OpenCV drawing error.
        draw_result(self.frame, box, replace(result, center_px=(-100., -100.)))

    def test_clipped_result_does_not_format_missing_depth(self):
        box = np.array([0, 100, 100, 200, .8, 32])
        result = Position(self.calibration_path).estimate(box, self.size)
        lines = draw_result(self.frame, box, result)
        self.assertTrue(any("Depth unavailable" in line for line in lines))
        self.assertFalse(any("Forward depth Z" in line for line in lines))

    def test_draw_text_renders_on_original_image(self):
        original = self.frame.copy()
        self.assertIsNone(draw_text(self.frame, ["First line", "Second line"]))
        self.assertFalse(np.array_equal(original, self.frame))
        unchanged = self.frame.copy()
        draw_text(self.frame, [])
        np.testing.assert_array_equal(self.frame, unchanged)
