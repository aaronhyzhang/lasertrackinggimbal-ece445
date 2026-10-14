from types import SimpleNamespace
from unittest.mock import MagicMock, patch

import numpy as np

from vision.detect import Detect
from vision.unit_tests.helpers import IMAGE, MODEL, VisionTest


class DetectTests(VisionTest):
    def setUp(self):
        super().setUp()
        self.model = MagicMock()
        self.model.names = {0: "person", 32: "sports ball"}
        self.model.predictor = None
        self.factory = self.stack.enter_context(patch("vision.detect.YOLO", return_value=self.model))
        self.detector = Detect(str(MODEL), confidence=.25, device="cpu")

    def predictions(self, coordinates, confidence, classes):
        boxes = MagicMock()
        boxes.__len__.return_value = len(confidence)
        for name, values in (("xyxy", coordinates), ("conf", confidence), ("cls", classes)):
            getattr(boxes, name).cpu.return_value.numpy.return_value = np.array(values)
        self.model.predict.return_value = [SimpleNamespace(boxes=boxes)]

    def test_highest_confidence_ball_is_selected_not_other_classes(self):
        self.predictions([[1, 2, 3, 4], [10, 20, 30, 40], [5, 6, 7, 8]], [.99, .8, .5], [0, 32, 32])
        box = self.detector.detect(self.frame)
        np.testing.assert_array_equal(box, [10, 20, 30, 40, .8, 32])
        kwargs = self.model.predict.call_args.kwargs
        self.assertEqual((kwargs["imgsz"], kwargs["device"], kwargs["conf"]), (640, "cpu", .25))
        self.assertTrue(kwargs["rect"])
        self.assertFalse(kwargs["verbose"])

    def test_missing_and_below_threshold_detections_return_empty_array(self):
        for coordinates, scores, classes in (([], [], []), ([[1, 2, 3, 4]], [.2], [32]),
                                               ([[1, 2, 3, 4]], [.9], [0])):
            self.predictions(coordinates, scores, classes)
            self.assertEqual(self.detector.detect(self.frame).size, 0)
        self.model.predict.return_value = [SimpleNamespace(boxes=None)]
        self.assertEqual(self.detector.detect(self.frame).size, 0)

    def test_image_path_is_accepted(self):
        self.predictions([], [], [])
        self.detector.detect(IMAGE)
        self.assertEqual(self.model.predict.call_args.args[0], str(IMAGE))

    def test_invalid_input_is_rejected(self):
        for image in (None, np.array([]), np.zeros((10, 10)), np.zeros((10, 10, 4))):
            with self.subTest(shape=getattr(image, "shape", None)), self.assertRaises(ValueError):
                self.detector.detect(image)
        with self.assertRaises(FileNotFoundError):
            self.detector.detect(self.directory / "missing.jpg")

    def test_invalid_configuration_is_rejected(self):
        with self.assertRaises(FileNotFoundError):
            Detect(str(self.directory / "missing.pt"))
        for kwargs in ({"confidence": 0}, {"confidence": 1.1}, {"confidence": np.nan},
                       {"image_size": 0}, {"image_size": True}, {"image_size": 640.5}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                Detect(str(MODEL), **kwargs)
        self.model.names = {0: "person"}
        with self.assertRaisesRegex(ValueError, "no 'sports ball'"):
            Detect(str(MODEL))

    def test_list_class_names_and_backend_device_reporting(self):
        self.model.names = ["person", " ball "]
        detector = Detect(str(MODEL))
        self.assertEqual(detector.ball_class_ids, [1])
        self.assertIsNone(detector.actual_device)
        self.model.predictor = SimpleNamespace(device="cuda:0")
        self.assertEqual(detector.actual_device, "cuda:0")
