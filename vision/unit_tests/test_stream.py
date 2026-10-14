from unittest.mock import MagicMock, patch

import cv2 as cv
import numpy as np

from vision.stream import Stream
from vision.unit_tests.helpers import VisionTest


class StreamTests(VisionTest):
    def setUp(self):
        super().setUp()
        self.camera = MagicMock()
        self.camera.isOpened.return_value = True
        self.camera.read.return_value = (True, self.frame)
        self.factory = self.stack.enter_context(patch("vision.stream.cv.VideoCapture", return_value=self.camera))

    def test_camera_mode_and_original_frame(self):
        stream = Stream("1", 992, 558, 30)
        self.factory.assert_called_once_with(1)
        settings = dict(call.args for call in self.camera.set.call_args_list)
        self.assertEqual(settings[cv.CAP_PROP_FRAME_WIDTH], 992)
        self.assertEqual(settings[cv.CAP_PROP_FRAME_HEIGHT], 558)
        self.assertEqual(settings[cv.CAP_PROP_FPS], 30)
        self.assertEqual(settings[cv.CAP_PROP_BUFFERSIZE], 1)
        self.assertNotIn(cv.CAP_PROP_AUTOFOCUS, settings)
        self.assertIs(stream.read(), self.frame)
        stream.release()
        self.camera.release.assert_called_once()

    def test_windows_video_path_does_not_receive_camera_settings(self):
        stream = Stream(r"C:\videos\ball.mp4")
        self.factory.assert_called_once_with(r"C:\videos\ball.mp4")
        self.camera.set.assert_not_called()
        self.assertIs(stream.read(), self.frame)

    def test_invalid_mode_is_rejected_before_opening_camera(self):
        for kwargs in ({"width": 0}, {"height": -1}, {"fps": 0}, {"fps": float("nan")}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                Stream("0", **kwargs)
        self.factory.assert_not_called()

    def test_open_failure_releases_camera(self):
        self.camera.isOpened.return_value = False
        with self.assertRaisesRegex(RuntimeError, "Could not open"):
            Stream("0")
        self.camera.release.assert_called_once()

    def test_bad_reads_raise_instead_of_returning_invalid_frames(self):
        stream = Stream("0")
        for value in ((False, self.frame), (True, None), (True, np.array([]))):
            self.camera.read.return_value = value
            with self.subTest(value=value[0]), self.assertRaises(RuntimeError):
                stream.read()

    def test_unsupported_driver_properties_are_nonfatal(self):
        self.camera.set.side_effect = [False, cv.error("unsupported"), True, True, True]
        self.assertIs(Stream("0").read(), self.frame)
        self.assertIn("did not accept", self.output.getvalue())

    def test_unexpected_setup_failure_releases_camera(self):
        self.camera.set.side_effect = RuntimeError("driver failed")
        with self.assertRaises(RuntimeError):
            Stream("0")
        self.camera.release.assert_called_once()
