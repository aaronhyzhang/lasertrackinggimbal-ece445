from unittest.mock import patch

import cv2 as cv
import numpy as np

from vision.utils import capture
from vision.unit_tests.helpers import VisionTest, mock_gui


class CaptureTests(VisionTest):
    def test_next_number_ignores_unrelated_files(self):
        self.assertEqual(capture.next_capture_number(self.directory), 1)
        for name in ("capture_0002.png", "capture_0010.png", "capture_bad.png", "other.png"):
            (self.directory / name).touch()
        self.assertEqual(capture.next_capture_number(self.directory), 11)

    def test_saved_png_is_lossless_and_collision_safe(self):
        path, number = capture.save_capture(self.frame, self.directory, 1)
        previous = path.read_bytes()
        second, next_number = capture.save_capture(self.frame, self.directory, 1)
        self.assertEqual((path.name, number, second.name, next_number),
                         ("capture_0001.png", 2, "capture_0002.png", 3))
        self.assertEqual(path.read_bytes(), previous)
        np.testing.assert_array_equal(cv.imread(str(path)), self.frame)

    def test_failed_png_encoding_raises(self):
        with patch("vision.utils.capture.cv.imencode", return_value=(False, None)):
            with self.assertRaises(RuntimeError):
                capture.save_capture(self.frame, self.directory, 1)

    def start_capture(self, keys):
        mock_gui(self.stack, keys)
        source = self.stack.enter_context(patch("vision.utils.capture.Stream")).return_value
        source.read.side_effect = lambda: self.frame.copy()
        return source

    def test_capture_main_saves_original_not_preview(self):
        source = self.start_capture((ord("c"), ord("q")))
        self.assertEqual(capture.main(["--output", str(self.directory)]), 0)
        np.testing.assert_array_equal(cv.imread(str(self.directory / "capture_0001.png")), self.frame)
        source.release.assert_called_once()

    def test_require_board_rejects_photo_without_charuco(self):
        self.start_capture((ord("c"), ord("q")))
        self.assertEqual(capture.main(["--output", str(self.directory), "--require-board"]), 0)
        self.assertFalse(list(self.directory.glob("capture_*.png")))
        self.assertIn("Not saved", self.output.getvalue())

    def test_capture_error_and_interrupt_release_source(self):
        source = self.start_capture(())
        source.read.side_effect = RuntimeError("disconnected")
        self.assertEqual(capture.main(["--output", str(self.directory)]), 1)
        source.release.assert_called_once()
        source.reset_mock()
        source.read.side_effect = KeyboardInterrupt()
        self.assertEqual(capture.main(["--output", str(self.directory)]), 0)
        source.release.assert_called_once()
