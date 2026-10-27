"""The one-command launcher end to end against the fake board (synthetic camera).

    cd host && py -3 -m unittest tests.test_run_gimbal -v
"""
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gimbal_bridge import fake_board, run_gimbal  # noqa: E402
from gimbal_bridge.g1 import SERVO_READY  # noqa: E402

try:
    import cv2  # noqa: F401
except ImportError:  # pragma: no cover
    cv2 = None

HERE = Path(__file__).resolve().parents[1]


class UI:
    def __init__(self, board, stop, run_s):
        self.board, self.stop, self.run_s = board, stop, run_s
        self.messages = []

    def info(self, message):
        self.messages.append(message)

    def confirm(self, message):
        pass  # "Enter" for a hand-set zero

    def wait_for_s1(self, message):
        threading.Timer(0.1, self.board.press_s1).start()
        if 'start tracking' in message:      # stop the run a while after arming
            threading.Timer(self.run_s, self.stop.set).start()


class Config(unittest.TestCase):
    def test_example_config_loads_and_overrides(self):
        cfg = run_gimbal.load_config(HERE / 'gimbal_config.example.json')
        self.assertEqual(cfg['board']['tilt_port'], 5005)
        self.assertEqual(run_gimbal.tracker_config(cfg).pan_limits, (15.0, 320.0))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'c.json'
            path.write_text(json.dumps({'tracking': {'gain': 0.5}, 'camera': {'hfov_deg': 60}}))
            cfg = run_gimbal.load_config(path)
        self.assertEqual(cfg['tracking']['gain'], 0.5)
        self.assertEqual(cfg['tracking']['pan_sign'], -1)        # untouched defaults survive
        self.assertIsNone(run_gimbal.load_config()['camera']['intrinsics'])
        self.assertAlmostEqual(run_gimbal.intrinsics(cfg).fx, 320 / 0.5773502691896, places=3)


@unittest.skipIf(cv2 is None, 'numpy/OpenCV not installed')
class Launcher(unittest.TestCase):
    def test_uncalibrated_board_to_tracking_and_clean_stop(self):
        board = fake_board.FakeBoard(positions=(90.0, 150.0), calibrated=False, cal_time_s=0.2).start()
        self.addCleanup(board.close)
        stop = threading.Event()
        ui = UI(board, stop, run_s=2.5)
        with tempfile.TemporaryDirectory() as d:
            cfg = run_gimbal.load_config()
            cfg['board'].update(host='127.0.0.1', tilt_port=board.ports[0], pan_port=board.ports[1])
            cfg['log_dir'] = d
            summary = run_gimbal.run(cfg, synthetic=True, ui=ui, stop_event=stop, status_every_s=0.2)
            logs = list(Path(d).glob('run_*.csv'))
            self.assertEqual(len(logs), 1)
            self.assertGreater(len(logs[0].read_text().splitlines()), 50)
        self.assertEqual(summary['calibration'], {'tilt': 'calibrated + zeroed by hand', 'pan': 'calibrated + homed'})
        self.assertGreater(summary['vision']['fps'], 15)
        self.assertGreater(summary['vision']['detection_rate'], 0.9)
        self.assertTrue(any('TRACKING' in m and 'board TRACKING' in m for m in ui.messages))
        self.assertEqual([a.state for a in board.axes], [SERVO_READY, SERVO_READY])  # DISABLE on exit

        # Second run: calibration already done, nothing to redo.
        stop = threading.Event()
        with tempfile.TemporaryDirectory() as d:
            cfg['log_dir'] = d
            summary = run_gimbal.run(cfg, synthetic=True, ui=UI(board, stop, run_s=0.5), stop_event=stop,
                                     status_every_s=0)
        self.assertEqual(set(summary['calibration'].values()), {'already ready'})

    def test_s2_during_tracking_stops_with_error(self):
        board = fake_board.FakeBoard(positions=(90.0, 150.0)).start()
        self.addCleanup(board.close)
        stop = threading.Event()
        ui = UI(board, stop, run_s=30)
        threading.Timer(1.5, board.press_s2).start()
        cfg = run_gimbal.load_config()
        cfg['board'].update(host='127.0.0.1', tilt_port=board.ports[0], pan_port=board.ports[1])
        with tempfile.TemporaryDirectory() as d:
            cfg['log_dir'] = d
            with self.assertRaises(run_gimbal.BridgeError):
                run_gimbal.run(cfg, synthetic=True, ui=ui, stop_event=stop, status_every_s=0)
        stop.set()


if __name__ == '__main__':
    unittest.main()
