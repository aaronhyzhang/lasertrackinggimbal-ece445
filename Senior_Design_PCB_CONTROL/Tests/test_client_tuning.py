import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from position_tuning import Recorder, metrics, recommend, read_config
import position_sweep
from test_client import Peer


class TuningTests(unittest.TestCase):
    def test_actual_udp_sweep_records_completed_move(self):
        peer = Peer()
        client = position_sweep.Client('127.0.0.1', peer.port)
        try:
            with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
                recorder = Recorder(folder, {'CONTROL_MAX_RPM': 100}, [90])
                position_sweep.run(client, 0, [90], 2, 2, recorder)
                recorder.finish()
                report = json.loads(recorder.base.with_suffix('.json').read_text())
                self.assertTrue(report['moves'][0]['passed'])
                self.assertGreaterEqual(report['moves'][0]['samples'], 4)
                self.assertEqual(peer.commands[-1], 'DISABLE')
        finally:
            client.close()
            peer.close()

    def test_negative_direction_overshoot_and_exclusion_of_arm_wait(self):
        move = dict(move=1, target_deg=90, passed=True, samples=[
            dict(time_s=0, state=5, position_deg=180, rpm=0),
            dict(time_s=10, state=7, position_deg=180, rpm=-20),
            dict(time_s=10.5, state=7, position_deg=87, rpm=-2),
            dict(time_s=11, state=7, position_deg=90, rpm=0),
            dict(time_s=11.5, state=7, position_deg=90, rpm=0)])
        result = metrics(move)
        self.assertEqual(result['overshoot_deg'], 3)
        self.assertEqual(result['duration_s'], 1.5)
        self.assertEqual(result['first_in_tolerance_s'], 1)
        self.assertEqual(result['direction'], -1)

    def test_one_parameter_advice_and_fault_priority(self):
        cfg = dict(CONTROL_ACCEL_RPM_S=100, CONTROL_MAX_RPM=100,
                   CONTROL_POSITION_KP=5, CONTROL_VELOCITY_KI_V=0.25)
        good = dict(passed=True, distance_deg=90, samples=20, max_sample_gap_s=0.08,
                    overshoot_deg=0.5, extra_settling_s=0.1, peak_rpm=60)
        advice = recommend([good], cfg, None)
        self.assertIn('CONTROL_ACCEL_RPM_S: 100 -> 110', advice)
        fast = {**good, 'peak_rpm': 95}
        self.assertIn('CONTROL_MAX_RPM: 100 -> 110', recommend([fast], cfg, None))
        overshoot = {**good, 'overshoot_deg': 4}
        self.assertIn('CONTROL_VELOCITY_KI_V', recommend([overshoot], cfg, None))
        self.assertIn('100 -> 80', recommend([good], cfg, 'Controller fault=8'))
        self.assertNotIn('Try only', recommend([good], cfg, 'communication timeout'))

    def test_sparse_observations_do_not_suggest_speed_increase(self):
        result = dict(passed=True, distance_deg=90, samples=10, max_sample_gap_s=0.5,
                      overshoot_deg=0, peak_rpm=60)
        self.assertIn('Telemetry gaps', recommend([result], {}, None))

    def test_settling_timeout_with_oscillation_gives_diagnostic_trial(self):
        samples = [dict(time_s=k*0.1, state=7, fault=0,
                        position_deg=p, rpm=0) for k, p in enumerate([80, 90, 96, 82, 96, 82])]
        result = metrics(dict(move=1, target_deg=90, passed=False, samples=samples))
        self.assertEqual(result['large_target_crossings'], 3)
        advice = recommend([result], {'CONTROL_VELOCITY_KI_V': 0.25},
                           'TimeoutError: Move/arm timed out; sequence cancelled')
        self.assertIn('CONTROL_VELOCITY_KI_V=0', advice)
        self.assertNotIn('Try only CONTROL_MAX_RPM', advice)

    def test_integral_off_stationary_offset_suggests_proportional_effort(self):
        samples = [dict(time_s=k*0.1, state=7, fault=0, position_deg=80.156, rpm=0)
                   for k in range(40)]
        result = metrics(dict(move=1, target_deg=90, passed=False, samples=samples))
        # Include the initial travel distance for recommendation eligibility.
        result['distance_deg'] = 90
        self.assertAlmostEqual(result['tail_mean_error_deg'], 9.844)
        advice = recommend([result], {'CONTROL_VELOCITY_KI_V': 0, 'CONTROL_VELOCITY_KP_V': 0.5},
                           'TimeoutError: Move/arm timed out; sequence cancelled')
        self.assertIn('CONTROL_VELOCITY_KP_V: 0.5 -> 0.65', advice)

    def test_partial_failure_still_writes_csv_and_report(self):
        with tempfile.TemporaryDirectory() as folder:
            recorder = Recorder(folder, {'CONTROL_ACCEL_RPM_S': 100}, [90])
            recorder('initial', status={'axis': 0})
            recorder('move', number=1, target=90000)
            recorder('sample', status=dict(state=7, fault=0, position_mdeg=20000,
                                           rpm_milli=12000, settled=0))
            with contextlib.redirect_stdout(io.StringIO()):
                recorder.finish('Controller fault=8')
            report = json.loads(recorder.base.with_suffix('.json').read_text())
            self.assertEqual(report['error'], 'Controller fault=8')
            self.assertFalse(report['moves'][0]['passed'])
            self.assertIn('100 -> 80', report['recommendation'])
            self.assertIn('20.0', recorder.base.with_suffix('.csv').read_text())

    def test_reads_current_local_config_labels(self):
        values = read_config(Path(__file__).resolve().parents[1]/'Core/Inc/control_config.h')
        self.assertGreater(values['CONTROL_ACCEL_RPM_S'], 0)
        self.assertGreater(values['CONTROL_POSITION_KP'], 0)

    def test_local_labels_follow_selected_motor_profile(self):
        path=Path(__file__).resolve().parents[1]/'Core/Inc/control_config.h'
        tilt=read_config(path,axis=0);pan=read_config(path,axis=1)
        self.assertEqual(tilt['CONTROL_POSITION_KP'],5.5)
        self.assertEqual(tilt['CONTROL_MAX_RPM'],110)
        self.assertEqual(tilt['CONTROL_ACCEL_RPM_S'],150)
        self.assertEqual(pan['CONTROL_POSITION_KP'],5)
        self.assertEqual(pan['CONTROL_ACCEL_RPM_S'],199.502)
        self.assertEqual(pan['CONTROL_VELOCITY_KI_V'],42.3)

    def test_successful_identical_plan_compares_previous_time(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder, 'position_000.json').write_text(json.dumps(dict(
                plan_deg=[90], error=None, moves=[dict(passed=True)], total_measured_move_s=2)))
            recorder = Recorder(folder, {}, [90])
            recorder.moves = [dict(move=1, target_deg=90, passed=True, samples=[
                dict(time_s=1, state=7, position_deg=80, rpm=20),
                dict(time_s=2, state=7, position_deg=90, rpm=0)])]
            with contextlib.redirect_stdout(io.StringIO()):
                recorder.finish()
            report = json.loads(recorder.base.with_suffix('.json').read_text())
            self.assertEqual(report['comparison']['time_change_percent'], -50)


if __name__ == '__main__':
    unittest.main()


