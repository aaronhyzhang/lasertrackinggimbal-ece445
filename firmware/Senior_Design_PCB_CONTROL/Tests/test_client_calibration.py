import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
import calibrate_motor


class CalibrationPeer:
    def __init__(self, states):
        self.states = iter(states)
        self.commands = []

    def request(self, op, value=None):
        self.commands.append(op)
        if op != 'STATUS':
            return 'OK'
        state = next(self.states)
        return dict(axis=0, source=1, enabled=1, state=state, fault=int(state == 8),
                    calibrated=int(state == 5), zero=0)


class CalibrationTests(unittest.TestCase):
    def test_enter_captures_zero_without_arming(self):
        class ZeroPeer:
            def __init__(self):
                self.commands = []
                self.zero = False
            def request(self, op, value=None):
                self.commands.append(op)
                if op == 'ZERO':
                    self.zero = True
                    return 'OK'
                return dict(axis=0, source=1, state=5, calibrated=1, fault=0,
                            zero=int(self.zero), position_mdeg=0)
        peer = ZeroPeer()
        with patch('builtins.input', return_value='') as enter, contextlib.redirect_stdout(io.StringIO()):
            calibrate_motor.keyboard_zero(peer, 0)
        enter.assert_called_once()
        self.assertEqual(peer.commands, ['STATUS', 'ZERO', 'STATUS'])

    def test_running_cannot_capture_keyboard_zero(self):
        peer = CalibrationPeer([7])
        with patch('builtins.input', return_value=''), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(RuntimeError):
            calibrate_motor.keyboard_zero(peer, 0)
        self.assertNotIn('ZERO', peer.commands)

    def test_complete_only_after_active_state_then_ready(self):
        peer = CalibrationPeer([0, 0, 1, 2, 5])
        with contextlib.redirect_stdout(io.StringIO()) as output:
            calibrate_motor.run(peer, 0, 2)
        self.assertIn('PASS: electrical calibration complete', output.getvalue())
        self.assertEqual(peer.commands.count('CALIBRATE'), 1)
        self.assertIn('HEARTBEAT', peer.commands)
        self.assertEqual(peer.commands[-1], 'DISABLE')
        self.assertNotIn('POSITION', peer.commands)

    def test_running_rejected_without_calibration_or_disable(self):
        peer = CalibrationPeer([7])
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(RuntimeError):
            calibrate_motor.run(peer, 0, 2)
        self.assertEqual(peer.commands, ['STATUS'])

    def test_fault_stops_and_does_not_report_success(self):
        peer = CalibrationPeer([0, 1, 8])
        with contextlib.redirect_stdout(io.StringIO()) as output, self.assertRaises(RuntimeError):
            calibrate_motor.run(peer, 0, 2)
        self.assertNotIn('PASS:', output.getvalue())
        self.assertEqual(peer.commands[-1], 'DISABLE')


if __name__ == '__main__':
    unittest.main()
