import contextlib
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
import position_sweep
from test_client import Peer


class SweepTests(unittest.TestCase):
    def test_heartbeat_rejection_reports_underlying_travel_fault(self):
        class FaultPeer:
            def __init__(self):
                self.commands = []
                self.failed = False
            def request(self, op, value=None):
                self.commands.append(op)
                if op == 'HEARTBEAT':
                    self.failed = True
                    raise RuntimeError('ERR G1 1 2')
                if op == 'STATUS':
                    return dict(axis=0, source=1, enabled=1, calibrated=1, zero=1,
                                state=8 if self.failed else 5, fault=7 if self.failed else 0,
                                position_mdeg=-791, target_mdeg=2000, rpm_milli=0, settled=0)
                return 'OK'
        peer = FaultPeer()
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, 'SERVO_TRAVEL'):
            position_sweep.run(peer, 0, [2], 1, 1)
        self.assertEqual(peer.commands[-1], 'DISABLE')

    def test_plan_two_round_trips_and_randoms_are_bounded(self):
        plan = position_sweep.targets(2, 358, 6, 1)
        self.assertEqual(plan[:3], [2, 358, 2])
        self.assertEqual(plan[-3:], [2, 358, 2])
        self.assertEqual(len(plan), 12)
        self.assertTrue(all(2 <= p <= 358 for p in plan))
        self.assertEqual(plan, position_sweep.targets(2, 358, 6, 1))

    def test_actual_udp_multiple_moves_one_session_and_final_disable(self):
        peer = Peer()
        client = position_sweep.Client('127.0.0.1', peer.port)
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                position_sweep.run(client, 0, [2, 358, 2, 90, 2, 358, 2], 2, 2)
            self.assertEqual(peer.commands.count('POSITION'), 7)
            self.assertEqual(peer.commands.count('DISABLE'), 1)
            self.assertEqual(peer.commands[-1], 'DISABLE')
        finally:
            client.close()
            peer.close()

    def test_ack_without_settling_times_out_and_disables(self):
        peer = Peer(settle=False)
        client = position_sweep.Client('127.0.0.1', peer.port)
        try:
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(TimeoutError):
                position_sweep.run(client, 0, [90, 180], 0.01, 0.01)
            self.assertEqual(peer.commands.count('POSITION'), 1)
            self.assertEqual(peer.commands[-1], 'DISABLE')
        finally:
            client.close()
            peer.close()

    def test_measured_dwell_accepts_stationary_encoder_without_firmware_flag(self):
        peer = Peer(settle=False)
        client = position_sweep.Client('127.0.0.1', peer.port)
        try:
            with contextlib.redirect_stdout(io.StringIO()) as output:
                position_sweep.run(client, 0, [358], 2, 2)
            self.assertIn('encoder held target for 0.5 s', output.getvalue())
            self.assertEqual(peer.commands[-1], 'DISABLE')
        finally:
            client.close()
            peer.close()

    def test_stop_during_sequence_cancels_without_next_target(self):
        class StoppedPeer:
            def __init__(self):
                self.commands = []
                self.status_count = 0

            def request(self, op, value=None):
                self.commands.append(op)
                if op != 'STATUS':
                    return 'OK'
                self.status_count += 1
                state = 7 if self.status_count == 2 else 5
                return dict(axis=0, source=1, enabled=1, calibrated=1, zero=1,
                            fault=0, state=state, settled=0, position_mdeg=0,
                            target_mdeg=90000, rpm_milli=0)
        peer = StoppedPeer()
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, 'stopped'):
            position_sweep.run(peer, 0, [90, 180], 1, 1)
        self.assertEqual(peer.commands.count('POSITION'), 1)
        self.assertEqual(peer.commands[-1], 'DISABLE')

    def test_position_in_tolerance_while_moving_cannot_pass_dwell(self):
        peer = Peer(settle=False)
        client = position_sweep.Client('127.0.0.1', peer.port)
        request = client.request
        def moving_request(op, value=None):
            result = request(op, value)
            if op == 'STATUS':
                result['rpm_milli'] = 3000
            return result
        client.request = moving_request
        try:
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(TimeoutError):
                position_sweep.run(client, 0, [90, 180], 1, 0.7)
            self.assertEqual(peer.commands.count('POSITION'), 1)
            self.assertEqual(peer.commands[-1], 'DISABLE')
        finally:
            client.close()
            peer.close()


if __name__ == '__main__':
    unittest.main()
