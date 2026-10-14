import ast
import csv
import subprocess
import sys
from unittest.mock import patch

import cv2 as cv
import numpy as np

from vision import run
from vision.utils import draw, logging
from vision.unit_tests.helpers import ROOT, VISION, VisionTest, mock_gui


class RunTests(VisionTest):
    def setUp(self):
        super().setUp()
        self.path = self.directory / "run.csv"
        self.detector_factory = self.stack.enter_context(patch.object(run, "Detect"))
        self.detector = self.detector_factory.return_value
        self.detector.actual_device = "cpu"
        self.detector.inference_ms = 10.0
        self.detector.detect.return_value = np.array([100, 100, 200, 200, .9, 32])
        self.source = self.stack.enter_context(patch.object(run, "Stream")).return_value
        self.source.read.side_effect = lambda: self.frame.copy()

    def arguments(self):
        return ["--calibration", str(self.calibration_path), "--log-csv", str(self.path)]

    def rows(self):
        with self.path.open(newline="", encoding="utf-8") as saved:
            return list(csv.DictReader(saved))

    def test_run_imports_utilities_without_duplicate_definitions(self):
        self.assertIs(run.draw_result, draw.draw_result)
        self.assertIs(run.draw_text, draw.draw_text)
        self.assertIs(run.MeasurementLogger, logging.MeasurementLogger)
        tree = ast.parse((VISION / "run.py").read_text())
        definitions = {node.name for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef))}
        self.assertEqual(definitions, {"main"})
        self.assertNotIn("args.focus", (VISION / "run.py").read_text())

    def test_live_loop_logs_every_frame_including_misses_and_geometry_errors(self):
        mock_gui(self.stack, (0, 0, 0, ord("q")))
        self.detector.detect.side_effect = [np.array([]), np.array([0, 100, 100, 200, .9, 32]),
                                           np.array([100, 100, 200, 200, .9, 32]),
                                           np.array([100, 100, 50, 200, .9, 32])]
        run.main(self.arguments())
        rows = self.rows()
        self.assertEqual([row["frame_index"] for row in rows], ["1", "2", "3", "4"])
        self.assertEqual(rows[0]["detected"], "False")
        self.assertEqual((rows[1]["depth_reliable"], rows[1]["z_m"]), ("False", ""))
        self.assertGreater(float(rows[2]["z_m"]), 0)
        self.assertIn("positive", rows[3]["geometry_error"])
        self.source.release.assert_called_once()
        cv.destroyAllWindows.assert_called_once()

    def test_inference_device_arguments_are_forwarded(self):
        mock_gui(self.stack, (ord("q"),)*4)
        for device in ("cpu", "mps", "0", "cuda:0"):
            with self.subTest(device=device):
                run.main(["--calibration", str(self.calibration_path), "--device", device])
                self.assertEqual(self.detector_factory.call_args.kwargs["device"], device)
        self.assertIn("MPS was requested but is not active", self.output.getvalue())

    def test_read_failure_closes_csv_and_releases_source(self):
        mock_gui(self.stack)
        self.source.read.side_effect = RuntimeError("disconnected")
        with patch.object(run, "MeasurementLogger", wraps=logging.MeasurementLogger) as factory:
            with patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                run.main(self.arguments())
            self.assertEqual(error.exception.code, 1)
        self.source.release.assert_called_once()
        # Reopening and reading proves cleanup flushed the header, including on Windows.
        self.assertEqual(self.rows(), [])
        factory.assert_called_once()

    def test_keyboard_interrupt_releases_source(self):
        mock_gui(self.stack)
        self.source.read.side_effect = KeyboardInterrupt()
        run.main(["--calibration", str(self.calibration_path)])
        self.source.release.assert_called_once()

    def test_missing_image_fails_without_opening_camera(self):
        mock_gui(self.stack)
        with patch.object(cv, "imread", return_value=None), patch("sys.stderr"), self.assertRaises(SystemExit) as error:
            run.main(["--calibration", str(self.calibration_path), "--image", str(self.directory / "missing.jpg")])
        self.assertEqual(error.exception.code, 1)
        self.source.read.assert_not_called()


class EntryPointTests(VisionTest):
    def test_entry_points_import_in_fresh_python_processes(self):
        commands = [
            (ROOT, ["-m", "vision.run", "--help"]),
            (ROOT, ["vision/run.py", "--help"]),
            (VISION, ["run.py", "--help"]),
            (ROOT, ["-m", "vision.utils.capture", "--help"]),
            (ROOT, ["-m", "vision.utils.calibration", "--help"]),
            (ROOT, ["-m", "vision.utils.calibration", "capture", "--help"]),
        ]
        for cwd, arguments in commands:
            with self.subTest(arguments=arguments, cwd=cwd):
                completed = subprocess.run([sys.executable, *arguments], cwd=cwd,
                                           capture_output=True, text=True, timeout=45)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
