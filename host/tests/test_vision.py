"""Detector, intrinsics, camera calibration and the closed vision loop (needs numpy + OpenCV).

    cd host && py -3 -m unittest tests.test_vision -v
"""
import math
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    import cv2
    import numpy as np
except ImportError:  # pragma: no cover
    cv2 = None

if cv2 is not None:
    from gimbal_bridge import camcal, fake_board  # noqa: E402
    from gimbal_bridge.bridge import Bridge  # noqa: E402
    from gimbal_bridge.camera import SyntheticCamera  # noqa: E402
    from gimbal_bridge.camera_model import Intrinsics  # noqa: E402
    from gimbal_bridge.detector import ColorBallDetector, make_detector  # noqa: E402
    from gimbal_bridge.tracking import TrackerConfig  # noqa: E402
    from gimbal_bridge.vision import VisionLoop  # noqa: E402

YELLOW = (0, 230, 230)


def frame(*balls, size=(480, 640)):
    img = np.full((*size, 3), (40, 40, 40), np.uint8)
    for (u, v, r) in balls:
        cv2.circle(img, (u, v), r, YELLOW, -1, cv2.LINE_AA)
    return img


@unittest.skipIf(cv2 is None, 'numpy/OpenCV not installed')
class Detector(unittest.TestCase):
    def test_finds_ball_centre(self):
        d = ColorBallDetector().detect(frame((400, 120, 15)))
        self.assertIsNotNone(d)
        self.assertAlmostEqual(d.cx, 400, delta=0.5)
        self.assertAlmostEqual(d.cy, 120, delta=0.5)
        self.assertAlmostEqual(d.w, 30, delta=3)

    def test_rejects_empty_small_and_non_round(self):
        det = ColorBallDetector(min_area=60)
        self.assertIsNone(det.detect(frame()))
        self.assertIsNone(det.detect(frame((100, 100, 3))))
        img = frame()
        cv2.rectangle(img, (100, 100), (300, 120), YELLOW, -1)   # long yellow bar
        self.assertIsNone(det.detect(img))

    def test_prefers_the_larger_ball(self):
        d = ColorBallDetector().detect(frame((100, 100, 8), (500, 300, 20)))
        self.assertAlmostEqual(d.cx, 500, delta=1)

    def test_config_factory(self):
        det = make_detector({'type': 'color', 'hsv_ranges': [[[20, 80, 80], [40, 255, 255]]]})
        self.assertIsNotNone(det.detect(frame((320, 240, 12))))
        with self.assertRaises(ValueError):
            make_detector({'type': 'nope'})


@unittest.skipIf(cv2 is None, 'numpy/OpenCV not installed')
class CameraModel(unittest.TestCase):
    def test_signs_and_round_trip(self):
        k = Intrinsics.from_fov(640, 480, 70)
        self.assertEqual(k.angles(320, 240), (0.0, -0.0))
        pan, tilt = k.angles(500, 100)
        self.assertGreater(pan, 0)
        self.assertGreater(tilt, 0)
        u, v = k.project(12.5, -7.0)
        p, t = k.angles(u, v)
        self.assertAlmostEqual(p, 12.5, places=6)
        self.assertAlmostEqual(t, -7.0, places=6)
        self.assertAlmostEqual(k.angles(640, 240)[0], 35.0, places=6)  # edge of a 70 degree view

    def test_distortion_is_removed(self):
        k = Intrinsics(640, 480, 600.0, 600.0, 320.0, 240.0, [-0.25, 0.08, 0.001, -0.001, 0.0])
        for pan, tilt in ((15.0, 10.0), (-20.0, -12.0), (0.0, 18.0)):
            ray = np.array([[[math.tan(math.radians(pan)), -math.tan(math.radians(tilt)), 1.0]]])
            kmat = np.array([[k.fx, 0, k.cx], [0, k.fy, k.cy], [0, 0, 1]], np.float64)
            px, _ = cv2.projectPoints(ray, np.zeros(3), np.zeros(3), kmat, np.array(k.dist))
            got = k.angles(*px[0, 0])
            self.assertAlmostEqual(got[0], pan, delta=0.02)
            self.assertAlmostEqual(got[1], tilt, delta=0.02)
            naive = Intrinsics(640, 480, 600.0, 600.0, 320.0, 240.0).angles(*px[0, 0])
            self.assertGreater(abs(naive[0] - pan) + abs(naive[1] - tilt), 0.2)  # distortion matters

    def test_save_load_scale(self):
        import tempfile
        k = Intrinsics(1280, 720, 900.0, 905.0, 640.0, 360.0, [0.1, 0, 0, 0, 0], 0.3)
        with tempfile.TemporaryDirectory() as d:
            k.save(f'{d}/camera.json')
            self.assertEqual(Intrinsics.load(f'{d}/camera.json'), k)
        s = k.scaled(640, 360)
        self.assertEqual((s.fx, s.cx, s.cy), (450.0, 320.0, 180.0))


@unittest.skipIf(cv2 is None, 'numpy/OpenCV not installed')
class CameraCalibration(unittest.TestCase):
    def test_recovers_known_intrinsics(self):
        """Render checkerboard views through a known camera, then calibrate."""
        cols, rows, square, size = 9, 6, 25.0, (640, 480)
        truth = np.array([[620.0, 0, 330.0], [0, 615.0, 236.0], [0, 0, 1]])
        rng = np.random.default_rng(3)
        views = []
        for _ in range(14):
            rvec = rng.uniform(-0.45, 0.45, 3)
            tvec = np.array([rng.uniform(-60, 0), rng.uniform(-50, 0), rng.uniform(380, 600)])
            img = np.full((size[1], size[0]), 255, np.uint8)
            for i in range(-1, cols):
                for j in range(-1, rows):
                    if (i + j) % 2 == 0:
                        quad = np.array([[i, j, 0], [i + 1, j, 0], [i + 1, j + 1, 0], [i, j + 1, 0]], np.float64) * square
                        pts, _ = cv2.projectPoints(quad, rvec, tvec, truth, None)
                        cv2.fillConvexPoly(img, np.round(pts.reshape(-1, 2) * 16).astype(np.int32), 0, cv2.LINE_AA, 4)
            corners = camcal.find_corners(cv2.GaussianBlur(img, (3, 3), 0), cols, rows)
            if corners is not None:
                views.append(corners)
        self.assertGreaterEqual(len(views), 8)
        k = camcal.calibrate(views, cols, rows, square, size)
        self.assertAlmostEqual(k.fx, 620.0, delta=620 * 0.02)
        self.assertAlmostEqual(k.fy, 615.0, delta=615 * 0.02)
        self.assertAlmostEqual(k.cx, 330.0, delta=8)
        self.assertAlmostEqual(k.cy, 236.0, delta=8)
        self.assertLess(k.rms, 0.5)

    def test_too_few_views(self):
        with self.assertRaises(ValueError):
            camcal.calibrate([], 9, 6, 25.0, (640, 480))


@unittest.skipIf(cv2 is None, 'numpy/OpenCV not installed')
class ClosedLoop(unittest.TestCase):
    def test_camera_detector_bridge_board(self):
        board = fake_board.FakeBoard(positions=(90.0, 150.0)).start()
        self.addCleanup(board.close)
        bridge = Bridge(TrackerConfig(), '127.0.0.1', *board.ports, rate_hz=100, timeout=0.1)
        self.addCleanup(bridge.close)
        k = Intrinsics.from_fov(640, 480, 70)
        cam = SyntheticCamera(k, bridge.positions, (1, -1), amplitude_deg=8.0, period_s=6.0, fps=30)
        threading.Timer(0.15, board.press_s1).start()
        bridge.start(arm_timeout_s=3)
        loop = VisionLoop(cam, ColorBallDetector(), k, bridge).start()
        self.addCleanup(loop.stop)
        time.sleep(1.0)                                  # settle onto the ball
        errors = []
        for _ in range(60):
            ball_tilt, ball_pan = cam.ball(time.monotonic() - cam._t0)
            tilt, pan = bridge.positions()
            errors.append(max(abs(ball_tilt - tilt), abs(ball_pan - pan)))
            time.sleep(0.03)
        self.assertTrue(bridge.running(), bridge.error)
        self.assertEqual(bridge.mode, 'TRACKING')
        self.assertLess(sum(errors) / len(errors), 1.5)   # gimbal keeps the ball near the centre
        st = loop.stats()
        self.assertGreater(st['detection_rate'], 0.95)
        self.assertGreater(st['fps'], 15)
        self.assertLess(st['detect_ms'], 20)


if __name__ == '__main__':
    unittest.main()
