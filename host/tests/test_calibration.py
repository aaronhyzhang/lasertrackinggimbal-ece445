"""Calibration flow (calibration.py) against the fake board.

    cd host && py -3 -m unittest tests.test_calibration -v
"""
import sys
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gimbal_bridge import calibration, fake_board  # noqa: E402
from gimbal_bridge.calibration import CalibrationError, ensure_ready  # noqa: E402
from gimbal_bridge.g1 import G1Client, SERVO_READY, SERVO_FAULT  # noqa: E402


class ScriptedUI:
    """Answers prompts like a person: Enter for zero prompts, S1 shortly after asked."""

    def __init__(self, board, press_s1=True):
        self.board, self.press_s1 = board, press_s1
        self.messages, self.confirms, self.s1_requests = [], 0, 0

    def info(self, message):
        self.messages.append(message)

    def confirm(self, message):
        self.confirms += 1

    def wait_for_s1(self, message):
        self.s1_requests += 1
        if self.press_s1:
            threading.Timer(0.1, self.board.press_s1).start()


class CalibrationFlow(unittest.TestCase):
    def board(self, **kw):
        b = fake_board.FakeBoard(positions=(90.0, 150.0), **{'cal_time_s': 0.2, **kw}).start()
        self.addCleanup(b.close)
        clients = [G1Client('127.0.0.1', b.ports[0], 0.2), G1Client('127.0.0.1', b.ports[1], 0.2)]
        for c in clients:
            self.addCleanup(c.close)
        return b, clients

    def test_fresh_board_calibrates_tilt_by_hand_and_homes_pan(self):
        board, clients = self.board(calibrated=False)
        ui = ScriptedUI(board)
        actions = ensure_ready(clients, ui)
        self.assertEqual(actions, {'tilt': 'calibrated + zeroed by hand', 'pan': 'calibrated + homed'})
        self.assertEqual((ui.confirms, ui.s1_requests), (1, 1))
        self.assertEqual([a.state for a in board.axes], [SERVO_READY, SERVO_READY])
        self.assertAlmostEqual(board.axes[1].position, 0.0)          # pan homed to its stop
        self.assertEqual(board.stored, [[True, True], [True, True]])  # saved to "flash"

        # Power cycle: pan comes back with its zero, tilt only needs its zero set again.
        board.power_cycle()
        ui = ScriptedUI(board)
        actions = ensure_ready(clients, ui)
        self.assertEqual(actions, {'tilt': 'zeroed by hand', 'pan': 'restored'})
        self.assertEqual((ui.confirms, ui.s1_requests), (1, 0))

    def test_ready_board_needs_nothing(self):
        board, clients = self.board()
        writes = board.flash_writes
        actions = ensure_ready(clients, ScriptedUI(board))
        self.assertEqual(set(actions.values()), {'already ready'})
        self.assertEqual(board.flash_writes, writes)

    def test_fault_is_reported_not_cleared(self):
        board, clients = self.board()
        board.axes[1].state, board.axes[1].fault = SERVO_FAULT, 7
        with self.assertRaisesRegex(CalibrationError, 'pan: fault TRAVEL'):
            ensure_ready(clients, ScriptedUI(board))

    def test_s1_not_pressed_for_home(self):
        board, clients = self.board(calibrated=False)
        ui = ScriptedUI(board, press_s1=False)
        calibration.calibrate_axis(clients[1], 'pan', ui)
        with self.assertRaisesRegex(CalibrationError, 'S1 was not pressed'):
            calibration.home_axis(clients[1], 'pan', ui, s1_timeout_s=0.6)
        self.assertFalse(board.axes[1].home)          # DISABLE cancelled the queued HOME
        self.assertFalse(board.axes[1].zero)

    def test_heartbeats_keep_calibration_alive(self):
        board, clients = self.board(calibrated=False, cal_time_s=1.2)   # longer than the 500 ms lease
        calibration.calibrate_axis(clients[0], 'tilt', ScriptedUI(board))
        self.assertTrue(board.axes[0].calibrated)
        self.assertEqual(board.axes[0].fault, 0)

    def test_forget(self):
        board, clients = self.board()
        calibration.forget(clients, ScriptedUI(board))
        self.assertEqual(board.stored, [[False, False], [False, False]])
        self.assertEqual(clients[1].status()['stored'], 0)

    def test_older_firmware_falls_back_to_hand_zero(self):
        board, clients = self.board(calibrated=False, legacy=True)
        ui = ScriptedUI(board)
        actions = ensure_ready(clients, ui)
        self.assertEqual(actions['pan'], 'calibrated + zeroed by hand')
        self.assertEqual((ui.confirms, ui.s1_requests), (2, 0))


if __name__ == '__main__':
    unittest.main()
