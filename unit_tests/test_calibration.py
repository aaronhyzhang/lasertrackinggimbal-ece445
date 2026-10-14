import argparse
from unittest.mock import patch

import cv2 as cv
import numpy as np

from vision.utils import calibration
from vision.utils.calibration import Calibration
from unit_tests.helpers import VISION, VisionTest, mock_gui


class CalibrationTests(VisionTest):
    def test_board_defaults_and_invalid_configurations(self):
        board = Calibration()
        self.assertEqual((board.squares_x, board.squares_y), (5, 7))
        self.assertAlmostEqual(board.marker_length / board.square_length, .6)
        for kwargs in ({"squares_x": 1}, {"squares_y": 2.5}, {"square_length": 0},
                       {"marker_length": .04}, {"dictionary": "unknown"},
                       {"dictionary": "DICT_DOES_NOT_EXIST"},
                       {"squares_x": 20, "squares_y": 20, "dictionary": "DICT_4X4_50"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                Calibration(**kwargs)

    def test_generate_detect_and_corner_quality(self):
        board = Calibration()
        path = board.generate(self.directory / "board.png", pixels_per_square=150)
        frame = cv.imread(str(path))
        corners, ids = board.detect(frame)
        self.assertTrue(board.has_usable_corners(corners, ids))
        self.assertEqual(len(ids), 24)
        self.assertFalse(board.has_usable_corners(None, None))
        self.assertFalse(board.has_usable_corners(corners[:3], ids[:3]))
        collinear = np.array([0, 1, 2, 3], dtype=np.int32).reshape(-1, 1)
        self.assertFalse(board.has_usable_corners(corners[:4], collinear, min_corners=4))
        self.assertIn(b"pHYs", path.read_bytes())
        with self.assertRaises(FileExistsError):
            board.generate(path)
        board.generate(path, pixels_per_square=150, force=True)

    def test_generate_rejects_bad_output_and_encoder_failure(self):
        board = Calibration()
        for path, kwargs in ((self.directory / "board.jpg", {}),
                             (self.directory / "board.png", {"margin": -1}),
                             (self.directory / "board.png", {"pixels_per_square": 50})):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                board.generate(path, **kwargs)
        with patch("vision.utils.calibration.cv.imencode", return_value=(False, None)):
            with self.assertRaises(OSError):
                board.generate(self.directory / "board.png")

    def test_load_and_validate_image_size(self):
        loaded = Calibration.load(self.calibration_path)
        self.assertEqual(loaded.image_size, self.size)
        self.assertEqual(loaded.rms_error, 0.)
        loaded.validate_image_size(self.size)
        with self.assertRaises(ValueError):
            loaded.validate_image_size((1920, 1080))
        with self.assertRaises(ValueError):
            Calibration().validate_image_size(self.size)
        with self.assertRaises(FileNotFoundError):
            Calibration.load(self.directory / "missing.npz")

    def test_validate_arrays_rejects_invalid_intrinsics_distortion_size_and_rms(self):
        matrix = np.array([[1000., 0, 496], [0, 1000., 279], [0, 0, 1]])
        distortion = np.zeros(5)
        Calibration._validate_arrays(matrix, distortion, self.size, 0.)
        cases = [(np.eye(2), distortion, self.size, 0.),
                 (matrix * np.nan, distortion, self.size, 0.),
                 (matrix, np.zeros(3), self.size, 0.),
                 (matrix, distortion, (0, 558), 0.),
                 (matrix, distortion, (992.5, 558), 0.),
                 (matrix, distortion, self.size, -1.)]
        for values in cases:
            with self.subTest(values=str(values)), self.assertRaises(ValueError):
                Calibration._validate_arrays(*values)

    def test_corrupt_or_incomplete_calibration_is_rejected(self):
        bad = self.directory / "bad.npz"
        for data in ({"image_size": self.size},
                     {"camera_matrix": np.eye(3), "dist_coeffs": np.zeros(5), "image_size": self.size,
                      "squares_x": [5, 7]}):
            np.savez(bad, **data)
            with self.assertRaises(ValueError):
                Calibration.load(bad)
        bad.write_bytes(b"not a calibration archive")
        with self.assertRaises(ValueError):
            Calibration.load(bad)

    def test_calibrate_recovers_known_intrinsics_from_projected_views(self):
        board = Calibration()
        known = Calibration.load(self.calibration_path).camera_matrix
        corners = board.board.getChessboardCorners()
        ids = np.arange(len(corners), dtype=np.int32).reshape(-1, 1)
        detections = []
        for index in range(12):
            path = self.directory / f"capture_{index:04d}.png"
            self.assertTrue(cv.imwrite(str(path), self.frame))
            rotation = np.array([-.3 + .05*index, .2*np.sin(index), .1*np.cos(index)])
            translation = np.array([-.05 + .003*index, -.07, .5 + .03*index])
            projected, _ = cv.projectPoints(corners, rotation, translation, known, np.zeros(5))
            detections.append((projected, ids))
        output = self.directory / "solved.npz"
        with patch.object(board, "detect", side_effect=detections):
            self.assertEqual(board.calibrate(self.directory, output=output), output)
        loaded = Calibration.load(output)
        np.testing.assert_allclose(loaded.camera_matrix, known, rtol=1e-3, atol=.01)
        self.assertLess(loaded.rms_error, .001)
        with np.load(output) as data:
            self.assertEqual(int(data["accepted_views"]), 12)
            self.assertEqual(len(data["per_view_errors"]), 12)

    def test_calibrate_rejects_missing_empty_and_unusable_datasets(self):
        board = Calibration()
        with self.assertRaises(FileNotFoundError):
            board.calibrate(self.directory / "missing")
        with self.assertRaisesRegex(ValueError, "No images match"):
            board.calibrate(self.directory)
        cv.imwrite(str(self.directory / "capture_0001.png"), self.frame)
        with self.assertRaisesRegex(ValueError, "Need at least"):
            board.calibrate(self.directory)
        for kwargs in ({"min_views": 2}, {"min_corners": 2}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                board.calibrate(self.directory, **kwargs)

    def test_board_arguments_construct_matching_configuration(self):
        parser = argparse.ArgumentParser()
        calibration.add_board_arguments(parser)
        args = parser.parse_args(["--square-length", ".04", "--marker-length", ".025", "--legacy-pattern"])
        board = calibration.calibration_from_args(args)
        self.assertEqual((board.square_length, board.marker_length, board.legacy_pattern), (.04, .025, True))

    def test_verify_uses_real_undistortion_and_releases_camera(self):
        mock_gui(self.stack)
        source = self.stack.enter_context(patch("vision.stream.Stream")).return_value
        source.read.return_value = self.frame
        Calibration.load(self.calibration_path).verify()
        source.release.assert_called_once()
        cv.imshow.assert_called_once()
        with self.assertRaises(ValueError):
            Calibration().verify()

    def test_calibration_main_generate_capture_calibrate_and_verify_dispatch(self):
        board_path = self.directory / "board.png"
        self.assertEqual(calibration.main(["generate", "--output", str(board_path)]), 0)
        self.assertTrue(board_path.exists())
        with patch("vision.utils.capture.main", return_value=0) as capture_main:
            self.assertEqual(calibration.main(["capture", "--camera", "1"]), 0)
            capture_main.assert_called_once_with(["--camera", "1"])
        with patch.object(Calibration, "calibrate") as solve:
            self.assertEqual(calibration.main(["calibrate", "--images", str(self.directory)]), 0)
            solve.assert_called_once()
        with patch.object(Calibration, "verify") as verify:
            self.assertEqual(calibration.main(["verify", "--calibration", str(self.calibration_path)]), 0)
            verify.assert_called_once()
        with patch.object(Calibration, "verify", side_effect=KeyboardInterrupt):
            self.assertEqual(calibration.main(["verify", "--calibration", str(self.calibration_path)]), 0)

    def test_calibration_main_reports_failure(self):
        with patch("sys.stderr"):
            self.assertEqual(calibration.main(["calibrate", "--images", str(self.directory / "missing")]), 1)

    def test_artifact_defaults_stay_under_vision(self):
        self.assertEqual(calibration.DEFAULT_CALIBRATION, VISION / "calibration/camera_calibration.npz")
        self.assertEqual(calibration.DEFAULT_IMAGES, VISION / "media")
