import csv
from dataclasses import replace
from unittest.mock import MagicMock, patch

import numpy as np

from vision.position import Position
from vision.utils.logging import CSV_FIELDS, Measurement, MeasurementLogger
from unit_tests.helpers import VisionTest


class LoggingTests(VisionTest):
    def setUp(self):
        super().setUp()
        self.path = self.directory / "measurements/run.csv"
        self.logger = MeasurementLogger(str(self.path))
        self.addCleanup(self.logger.close)
        box = np.array([100, 100, 200, 200, .8, 32])
        result = Position(self.calibration_path).estimate(box, self.size)
        self.measurement = Measurement(1, .1, box, result, None, 10., 20., 50., .24,
                                       640, 2., 8., None, "cpu")

    def rows(self):
        self.logger.file.flush()
        with self.path.open(newline="", encoding="utf-8") as saved:
            return list(csv.DictReader(saved))

    def test_schema_position_and_timing_values(self):
        self.logger.log(self.measurement, flush=True)
        row, = self.rows()
        self.assertEqual(tuple(row), CSV_FIELDS)
        self.assertEqual(row["detected"], "True")
        self.assertEqual(row["device_requested"], "auto")
        self.assertEqual(float(row["z_m"]), self.measurement.result.z_m)
        self.assertAlmostEqual(float(row["center_u_px"]), 150.)
        self.assertEqual(float(row["inference_ms"]), 10.)

    def test_misses_and_geometry_errors_are_logged(self):
        self.logger.log(replace(self.measurement, bbox=np.array([]), result=None))
        self.logger.log(replace(self.measurement, result=None, geometry_error="invalid geometry"))
        missed, error = self.rows()
        self.assertEqual((missed["detected"], missed["z_m"], missed["confidence"]), ("False", "", ""))
        self.assertEqual((error["detected"], error["geometry_error"], error["z_m"]),
                         ("True", "invalid geometry", ""))

    def test_existing_csv_is_not_overwritten_and_close_flushes(self):
        self.logger.log(self.measurement)
        self.logger.close()
        self.assertTrue(self.logger.file.closed)
        before = self.path.read_bytes()
        with self.assertRaises(FileExistsError):
            MeasurementLogger(str(self.path))
        self.assertEqual(self.path.read_bytes(), before)

    def test_timed_and_forced_flush(self):
        with patch.object(self.logger, "file") as output, patch("vision.utils.logging.time.perf_counter", side_effect=[2., 2.2, 2.3]):
            self.logger.log(self.measurement)
            self.logger.log(self.measurement)
            self.assertEqual(output.flush.call_count, 1)
            self.logger.log(self.measurement, flush=True)
            self.assertEqual(output.flush.call_count, 2)

    def test_failed_header_closes_the_file(self):
        fake_file = MagicMock()
        with patch("pathlib.Path.open", return_value=fake_file), patch("vision.utils.logging.csv.DictWriter", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                MeasurementLogger(str(self.directory / "failed.csv"))
        fake_file.close.assert_called_once()
