"""Host tests: tracking math, G1 client, bridge and R&V tools against the fake board.

    cd host && py -3 -m unittest discover -s tests -v
"""
import math
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gimbal_bridge import fake_board, rv  # noqa: E402
from gimbal_bridge.bridge import Bridge, BridgeError  # noqa: E402
from gimbal_bridge.g1 import G1Client, G1Error, SERVO_RUNNING, SERVO_READY, SERVO_FAULT  # noqa: E402
from gimbal_bridge.tracking import Observation, TrackerConfig, next_target, pixel_to_angles  # noqa: E402


class TrackingMath(unittest.TestCase):
    def test_pixel_angles_signs(self):
        pan, tilt = pixel_to_angles(400, 100, fx=600, fy=600, cx=320, cy=240)
        self.assertGreater(pan, 0)   # right of centre
        self.assertGreater(tilt, 0)  # above centre
        self.assertAlmostEqual(pan, math.degrees(math.atan2(80, 600)))
        self.assertEqual(pixel_to_angles(320, 240, 600, 600, 320, 240), (0.0, 0.0))

    def test_next_target(self):
        cfg = TrackerConfig(gain=0.5, deadband_deg=1.0, max_step_deg=5.0)
        lim = (15, 320)
        self.assertEqual(next_target(100, 0.5, -1, cfg, lim, 123.0), 123.0)       # inside deadband: hold
        self.assertAlmostEqual(next_target(100, 4.0, -1, cfg, lim, 0), 98.0)     # ball right -> pan decreases
        self.assertAlmostEqual(next_target(100, 4.0, 1, cfg, lim, 0), 102.0)
        self.assertAlmostEqual(next_target(100, 40.0, 1, cfg, lim, 0), 105.0)    # step limited
        self.assertEqual(next_target(16, 40.0, -1, cfg, lim, 0), 15)             # clamped to window
        self.assertEqual(next_target(100, float('nan'), 1, cfg, lim, 7.0), 7.0)

    def test_config_validation(self):
        with self.assertRaises(ValueError):
            TrackerConfig(pan_limits=(10, 340)).validate()  # pan firmware max is 337
        with self.assertRaises(ValueError):
            TrackerConfig(pan_sign=0).validate()
        TrackerConfig().validate()


class WithBoard(unittest.TestCase):
    def setUp(self):
        self.board = fake_board.FakeBoard(positions=(90.0, 150.0)).start()

    def tearDown(self):
        self.board.close()

    def bridge(self, **cfg):
        b = Bridge(TrackerConfig(**cfg), '127.0.0.1', *self.board.ports, rate_hz=100, timeout=0.1)
        self.addCleanup(b.close)
        return b

    def start(self, bridge):
        """Run start() while 'pressing S1' shortly after the prompt."""
        pressed = threading.Event()

        def prompt():
            threading.Timer(0.15, lambda: (self.board.press_s1(), pressed.set())).start()
        bridge.start(arm_timeout_s=3, on_waiting_for_s1=prompt)
        self.assertTrue(pressed.is_set())


class Client(WithBoard):
    def test_status_and_errors(self):
        c = G1Client('127.0.0.1', self.board.ports[1], timeout=0.2)
        self.addCleanup(c.close)
        s = c.status()
        self.assertEqual((s['axis'], s['state'], s['dual']), (1, SERVO_READY, 1))
        with self.assertRaises(G1Error):
            c.heartbeat()  # not owner yet
        c.position(100)
        with self.assertRaises(G1Error):
            c.position(338)  # pan accepts at most 337 degrees
        self.assertEqual(c.disable(), 'OK DISABLED')

    def test_second_session_cannot_steal_live_lease(self):
        a = G1Client('127.0.0.1', self.board.ports[0], 0.2)
        b = G1Client('127.0.0.1', self.board.ports[0], 0.2)
        self.addCleanup(a.close)
        self.addCleanup(b.close)
        a.position(100)
        with self.assertRaises(G1Error) as e:
            b.position(120)
        self.assertEqual(e.exception.reply, 'ERR owner')


class BridgeFlow(WithBoard):
    def test_arms_without_jump_and_tracks(self):
        br = self.bridge(gain=1.0, deadband_deg=0.2, pan_sign=-1, tilt_sign=1)
        self.start(br)
        self.assertEqual([a.state for a in self.board.axes], [SERVO_RUNNING, SERVO_RUNNING])
        self.assertAlmostEqual(self.board.axes[0].target, 90.0, places=2)   # held current position
        self.assertAlmostEqual(self.board.axes[1].target, 150.0, places=2)
        ball = (100.0, 130.0)  # tilt, pan in gimbal coordinates
        for _ in range(60):
            tilt, pan = br.positions()
            br.submit(Observation(True, (ball[1] - pan) * -1, (ball[0] - tilt) * 1))
            time.sleep(0.02)
        self.assertEqual(br.mode, 'TRACKING')
        self.assertAlmostEqual(self.board.axes[0].position, 100.0, delta=0.5)
        self.assertAlmostEqual(self.board.axes[1].position, 130.0, delta=0.5)

    def test_lost_target_holds_and_keeps_lease(self):
        br = self.bridge()
        self.start(br)
        br.submit(Observation(False))
        time.sleep(1.2)  # longer than the 500 ms lease: heartbeats must keep it alive
        self.assertTrue(br.running(), br.error)
        self.assertEqual(br.mode, 'SEARCHING')
        self.assertEqual([a.state for a in self.board.axes], [SERVO_RUNNING, SERVO_RUNNING])

    def test_stale_vision_counts_as_lost(self):
        br = self.bridge()
        self.start(br)
        br.submit(Observation(True, 5.0, 5.0, t_capture=time.monotonic() - 1.0))
        time.sleep(0.1)
        self.assertEqual(br.mode, 'SEARCHING')
        self.assertAlmostEqual(self.board.axes[1].target, 150.0, places=2)

    def test_s2_stops_bridge(self):
        br = self.bridge()
        self.start(br)
        self.board.press_s2()
        time.sleep(0.2)
        self.assertFalse(br.running())
        self.assertIsInstance(br.error, BridgeError)

    def test_stop_disables_both(self):
        br = self.bridge()
        self.start(br)
        br.stop()
        self.assertEqual([a.state for a in self.board.axes], [SERVO_READY, SERVO_READY])

    def test_bridge_death_trips_lease(self):
        br = self.bridge()
        self.start(br)
        br.disable = lambda: None  # simulate the laptop loop dying without DISABLE...
        br._stop.set()
        br._thread.join()
        time.sleep(0.7)         # ...the firmware lease must stop the motors on its own
        self.assertIn(SERVO_FAULT, [a.state for a in self.board.axes])

    def test_refuses_when_not_ready(self):
        self.board.axes[1].state = SERVO_FAULT
        self.board.axes[1].fault = 7
        br = self.bridge()
        with self.assertRaises(BridgeError):
            br.start(arm_timeout_s=0.5)
        self.assertFalse(self.board.axes[0].valid)  # no target was queued

    def test_refuses_outside_window(self):
        self.board.axes[1].position = 330.0
        with self.assertRaises(BridgeError):
            self.bridge().start(arm_timeout_s=0.5)

    def test_no_s1_times_out_and_disables(self):
        br = self.bridge()
        with self.assertRaises(BridgeError):
            br.start(arm_timeout_s=0.4)
        self.assertFalse(any(a.valid for a in self.board.axes))


class SystemState(WithBoard):
    def test_target_reports_drive_board_state(self):
        br = self.bridge()
        self.start(br)
        self.assertTrue(br.send_target)
        br.submit(Observation(True, 0.0, 0.0))
        time.sleep(0.1)
        self.assertGreater(self.board.targets, 0)
        self.assertEqual(self.board.system_state(), fake_board.SYS_TRACKING)
        self.assertEqual(br.system_state, fake_board.SYS_TRACKING)
        br.submit(Observation(False))
        time.sleep(0.1)
        self.assertEqual(self.board.system_state(), fake_board.SYS_SEARCHING)
        self.assertEqual(br.system_state, fake_board.SYS_SEARCHING)

    def test_target_requires_owner(self):
        c = G1Client('127.0.0.1', self.board.ports[0], 0.2)
        self.addCleanup(c.close)
        with self.assertRaises(G1Error) as e:
            c.target(True)
        self.assertEqual(e.exception.reply, 'ERR owner')
        self.assertEqual(self.board.targets, 0)


class LegacyFirmware(unittest.TestCase):
    def test_bridge_skips_target_without_sys_field(self):
        board = fake_board.FakeBoard(legacy=True).start()
        self.addCleanup(board.close)
        br = Bridge(TrackerConfig(), '127.0.0.1', *board.ports, rate_hz=100, timeout=0.1)
        self.addCleanup(br.close)
        threading.Timer(0.15, board.press_s1).start()
        br.start(arm_timeout_s=3)
        br.submit(Observation(True, 1.0, 1.0))
        time.sleep(0.3)
        self.assertTrue(br.running(), br.error)
        self.assertFalse(br.send_target)
        self.assertIsNone(br.system_state)
        self.assertEqual(board.targets, 0)


class RV(WithBoard):
    def test_reject_all_malformed(self):
        c = G1Client('127.0.0.1', self.board.ports[0], 0.2)
        self.addCleanup(c.close)
        r = rv.reject(c)
        self.assertTrue(r['passed'], r['failures'])
        self.assertEqual(self.board.rejected, len(rv.MALFORMED))

    def test_latency_and_throughput_report(self):
        c = G1Client('127.0.0.1', self.board.ports[0], 0.2)
        self.addCleanup(c.close)
        r = rv.latency(c, 50, 0)
        self.assertEqual(r['received'], 50)
        self.assertTrue(r['passed'])
        t = rv.throughput('127.0.0.1', self.board.ports[0], 0.5, 4)
        self.assertGreater(t['requests'], 10)


if __name__ == '__main__':
    unittest.main()
