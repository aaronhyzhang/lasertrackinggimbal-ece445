import argparse
import contextlib
import io
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Tools'))
import commission
import commission_model


class Peer:
    def __init__(self,gap=False):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1',0));self.sock.settimeout(0.1)
        self.port=self.sock.getsockname()[1];self.done=False;self.commands=[]
        self.config=dict(schema=1,revision=1,axis=0,source=1,current_pi=0,
                         velocity_kp=500,velocity_ki=500,position_kp=5000,
                         max_rpm=100000,accel_rpm_s=75000,run_volts=6000)
        self.trial=0;self.active=False;self.read=0;self.gap=gap
        self.thread=threading.Thread(target=self.loop);self.thread.start()
    def loop(self):
        while not self.done:
            try:packet,address=self.sock.recvfrom(2048)
            except socket.timeout:continue
            words=packet.decode().split();op=words[3];self.commands.append(op)
            response=f'OK G1 {words[1]} {words[2]}'.encode()
            if op=='CONFIG':response=('C1 '+' '.join(f'{k}={v}' for k,v in self.config.items())).encode()
            elif op=='STATUS':response=b'S1 axis=0 source=1 enabled=1 state=5 fault=0 calibrated=1 zero=1 position_mdeg=180000'
            elif op=='SET':
                self.config[commission.KEYS[int(words[4])]]=int(words[5]);self.config['revision']+=1
            elif op=='TEST':self.active=True;self.trial+=1;self.read=0
            elif op=='TESTINFO':response=f'T1 trial={self.trial} aborted=0 complete={int(self.read>=16)} next=16'.encode()
            elif op=='TRACE':
                first=int(words[4])+(1 if self.gap else 0);count=min(8,16-first)
                response=struct.pack('<6I',0x31525443,1,self.trial,first,count,16)
                for i in range(first,first+count):
                    row=dict.fromkeys(commission.FIELDS,0)
                    row.update(sequence=i,us=i*1000,encoder_us=i*1000,trial=self.trial,
                               config_revision=self.config['revision'],state=7,flags=14,
                               position=180,velocity=0,bus_v=24)
                    response+=commission.ROW.pack(*(row[key] for key in commission.FIELDS))
                self.read=first+count
            elif op=='DISABLE':response=b'OK DISABLED'
            self.sock.sendto(response,address)
    def close(self):self.done=True;self.thread.join();self.sock.close()


class CommissionClientTests(unittest.TestCase):
    def args(self):
        return argparse.Namespace(axis=0,mode='voltage',amplitude=0.3,period_ms=100,
                                  duration=1,centre=180,radius=30,arm_timeout=1)
    def test_actual_udp_config_write_readback_and_trace_capture(self):
        peer=Peer();board=commission.Board('127.0.0.1',peer.port)
        try:
            active=commission.apply(board,dict(velocity_kp=0.65))
            self.assertEqual(active['velocity_kp'],650)
            with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()):
                rows,meta,path=commission.capture(board,self.args(),folder)
                self.assertEqual(len(rows),16);self.assertTrue(meta['completed'])
                self.assertTrue(path.exists());self.assertTrue(path.with_suffix('.svg').exists())
            board.finish();self.assertEqual(peer.commands[-1],'DISABLE')
        finally:peer.close()
    def test_trace_gap_fails_and_preserves_partial_metadata(self):
        peer=Peer(gap=True);board=commission.Board('127.0.0.1',peer.port)
        try:
            with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError,'overwritten'):
                    commission.capture(board,self.args(),folder)
                report=json.loads(next(Path(folder).glob('*.json')).read_text())
                self.assertFalse(report['completed']);self.assertIn('overwritten',report['error'])
            board.finish()
        finally:peer.close()
    def test_cli_set_releases_owner_for_the_next_test_without_reverting_gains(self):
        peer=Peer()
        try:
            argv=['commission.py','set','--host','127.0.0.1','--port',str(peer.port),'--velocity-kp','0.215']
            with patch.object(sys,'argv',argv),contextlib.redirect_stdout(io.StringIO()):
                commission.main()
            self.assertEqual(peer.commands[-1],'DISABLE')
            self.assertEqual(peer.config['velocity_kp'],215)
        finally:peer.close()
    def test_auto_stages_velocity_before_position_and_restores_baseline(self):
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS})
        applied=[];captured=[]
        candidate=dict(velocity_kp=0.6,velocity_ki=0.2,position_kp=3)
        args=self.args();args.max_trials=1;args.test_degrees=10;args.max_overshoot=2
        def fake_apply(board,changes):
            applied.append(dict(changes))
            return {**baseline,**{k:round(v*1000) for k,v in changes.items()}}
        def fake_capture(board,args,folder,**kwargs):
            captured.append(kwargs.get('mode'))
            return [],dict(score=dict(passed=True,worst_settling_s=1,worst_overshoot_deg=0.1)),Path(folder,'data.csv')
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',side_effect=fake_apply), \
             patch.object(commission,'capture',side_effect=fake_capture), \
             patch.object(commission_model,'identify',return_value=dict(accepted=True,candidates=[candidate])), \
             patch.object(commission_model,'score_velocity',return_value=dict(passed=True,rmse_rad_s=0.1)):
            commission.auto(object(),args,folder)
            self.assertEqual(captured[:3],['prbs','position','velocity'])
            self.assertEqual(set(applied[0]),{'velocity_kp','velocity_ki'})
            self.assertEqual(applied[-1],{key:1 for key in commission.KEYS})
            self.assertTrue(json.loads(Path(folder,'auto_report.json').read_text())['recommendation_verified'])

    def test_saved_capture_requires_completion_and_matching_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder,'capture.csv');path.write_text('sequence,us\n0,1000\n')
            context=dict(completed=True,error=None,mode='voltage',config=dict(axis=0,run_volts=6000))
            path.with_suffix('.json').write_text(json.dumps(context))
            rows,_=commission.completed_capture(path,context['config'])
            self.assertEqual(len(rows),1)
            with self.assertRaisesRegex(RuntimeError,'run_volts'):
                commission.completed_capture(path,dict(axis=0,run_volts=5000))
            context['completed']=False
            path.with_suffix('.json').write_text(json.dumps(context))
            with self.assertRaisesRegex(RuntimeError,'incomplete'):
                commission.completed_capture(path)

    def test_saved_pan_capture_requires_matching_limits_and_polarity(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder,'capture.csv');path.write_text('sequence,us\n0,1000\n')
            config=dict(axis=1,min_mdeg=2000,max_mdeg=337000,encoder_sign=-1)
            path.with_suffix('.json').write_text(json.dumps(dict(completed=True,error=None,mode='voltage',config=config)))
            commission.completed_capture(path,config)
            with self.assertRaisesRegex(RuntimeError,'polarity'):
                commission.completed_capture(path,{**config,'encoder_sign':1})

    def test_capture_rejects_window_beyond_motor_limits_before_test(self):
        peer=Peer();peer.config['max_mdeg']=337000
        board=commission.Board('127.0.0.1',peer.port);args=self.args()
        args.centre=330;args.radius=20
        try:
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaisesRegex(RuntimeError,'command limits'):
                    commission.capture(board,args,folder)
            self.assertNotIn('TEST',peer.commands)
            board.finish()
        finally:peer.close()

    def test_auto_reuses_identification_trace_without_another_voltage_test(self):
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS})
        args=self.args();args.trace=Path('saved.csv');args.max_trials=1
        args.test_degrees=10;args.max_overshoot=2
        captured=[]
        def fake_capture(board,args,folder,**kwargs):
            captured.append(kwargs.get('mode'))
            return [],dict(score=dict(passed=True,worst_settling_s=1,worst_overshoot_deg=0.1)),Path(folder,'trial.csv')
        candidate=dict(velocity_kp=0.6,velocity_ki=0.2,position_kp=3)
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'completed_capture',return_value=([],{})) as reuse, \
             patch.object(commission,'apply',return_value=baseline), \
             patch.object(commission,'capture',side_effect=fake_capture), \
             patch.object(commission_model,'identify',return_value=dict(accepted=True,candidates=[candidate])), \
             patch.object(commission_model,'score_velocity',return_value=dict(passed=True,rmse_rad_s=0.1)):
            commission.auto(object(),args,folder)
            reuse.assert_called_once_with(args.trace,baseline)
            self.assertEqual(captured[:2],['position','velocity'])
            self.assertNotIn('prbs',captured)

    def test_verify_position_compares_baseline_holds_out_and_restores(self):
        args=self.args();args.velocity_kp=0.215;args.velocity_ki=18.795
        args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS})
        active={**baseline,'velocity_kp':215,'velocity_ki':18795}
        calls=[]
        def capture(board,args,folder,**kwargs):
            calls.append(kwargs)
            score=dict(passed=True,worst_settling_s=2 if kwargs['label']=='baseline' else 1,worst_overshoot_deg=0.2)
            return [],dict(score=score),Path(folder,kwargs['label']+'.csv')
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',side_effect=[active,baseline]) as applied, \
             patch.object(commission,'capture',side_effect=capture):
            commission.verify(object(),args,folder)
            self.assertEqual(applied.call_args_list[0].args[1],dict(velocity_kp=0.215,velocity_ki=18.795))
            self.assertEqual(applied.call_args_list[-1].args[1],{k:1 for k in commission.KEYS})
            self.assertEqual([c['label'] for c in calls],['baseline','candidate','holdout_1','holdout_2'])
            self.assertEqual([c['amplitude'] for c in calls],[5,5,3.5,3.5])
            report=json.loads(Path(folder,'verify_report.json').read_text())
            self.assertTrue(report['recommendation_verified']);self.assertTrue(report['baseline_restored'])
            self.assertEqual(report['recommended_config']['velocity_ki'],18795)

    def test_verify_failed_position_gets_no_holdouts_or_recommendation(self):
        args=self.args();args.velocity_kp=0.215;args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS})
        score=dict(passed=False,worst_settling_s=1e6,worst_overshoot_deg=3)
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',return_value=baseline), \
             patch.object(commission,'capture',return_value=([],dict(score=score),Path(folder,'data.csv'))) as capture:
            commission.verify(object(),args,folder)
            self.assertEqual(capture.call_count,2)
            report=json.loads(Path(folder,'verify_report.json').read_text())
            self.assertIsNone(report['recommended_config']);self.assertFalse(report['recommendation_verified'])
            self.assertTrue(report['baseline_restored'])

    def test_verify_trial_failure_preserves_report_and_attempts_restore(self):
        args=self.args();args.velocity_kp=0.215;args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS})
        score=dict(passed=True,worst_settling_s=1,worst_overshoot_deg=0.1)
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',return_value=baseline) as applied, \
             patch.object(commission,'capture',side_effect=[([],dict(score=score),Path(folder,'baseline.csv')),
                                                          RuntimeError('Controller fault')]):
            with self.assertRaisesRegex(RuntimeError,'Controller fault'):
                commission.verify(object(),args,folder)
            self.assertEqual(applied.call_args_list[-1].args[1],{k:1 for k in commission.KEYS})
            report=json.loads(Path(folder,'verify_report.json').read_text())
            self.assertIn('Controller fault',report['error']);self.assertFalse(report['recommendation_verified'])

    def test_verify_rejects_limit_changes_before_a_trial(self):
        args=self.args();args.velocity_kp=0.215;args.max_rpm=200
        with self.assertRaisesRegex(ValueError,'limits unchanged'):
            commission.verify(object(),args,'unused')

    def test_envelope_recommends_fastest_passing_trial_and_restores(self):
        args=self.args();args.search='acceleration';args.ceiling=150;args.max_trials=2
        args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS});baseline['accel_rpm_s']=75000
        durations=iter((1.0,0.9,0.95,1.1,1.15))
        def capture(board,args,folder,**kwargs):
            score=dict(passed=True,worst_settling_s=next(durations),worst_overshoot_deg=0.1)
            return [dict(velocity=1)],dict(score=score),Path(folder,kwargs['label']+'.csv')
        def apply(board,changes):return {**baseline,**{k:round(v*1000) for k,v in changes.items()}}
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',side_effect=apply) as applied, \
             patch.object(commission,'capture',side_effect=capture):
            commission.envelope(object(),args,folder)
            report=json.loads(Path(folder,'envelope_report.json').read_text())
            self.assertEqual(report['recommended']['accel_rpm_s'],86250)
            self.assertEqual(report['highest_passed']['accel_rpm_s'],99187)
            self.assertTrue(report['baseline_restored'])
            self.assertEqual(applied.call_args_list[-1].args[1],dict(accel_rpm_s=75))

    def test_envelope_keeps_baseline_when_higher_limit_is_slower(self):
        args=self.args();args.search='acceleration';args.ceiling=150;args.max_trials=1
        args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS});baseline['accel_rpm_s']=75000
        durations=iter((1.0,1.1,1.2))
        def capture(board,args,folder,**kwargs):
            return [],dict(score=dict(passed=True,worst_settling_s=next(durations),worst_overshoot_deg=0.1)),Path(folder,'trace.csv')
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',return_value=baseline), \
             patch.object(commission,'capture',side_effect=capture):
            commission.envelope(object(),args,folder)
            report=json.loads(Path(folder,'envelope_report.json').read_text())
            self.assertEqual(report['recommended'],baseline)
            self.assertEqual(report['best_worst_settling_s'],1.0)

    def test_envelope_optimizes_arrival_when_stability_checks_both_pass(self):
        args=self.args();args.search='acceleration';args.ceiling=150;args.max_trials=1
        args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS});baseline['accel_rpm_s']=75000
        scores=iter((dict(passed=True,worst_arrival_s=0.275,worst_settling_s=0.905038,worst_overshoot_deg=0.01),
                     dict(passed=True,worst_arrival_s=0.198,worst_settling_s=0.896001,worst_overshoot_deg=0.098),
                     dict(passed=True,worst_arrival_s=0.200,worst_settling_s=0.905964,worst_overshoot_deg=0.098)))
        def capture(board,args,folder,**kwargs):
            return [],dict(score=next(scores)),Path(folder,'trace.csv')
        def apply(board,changes):return {**baseline,**{k:round(v*1000) for k,v in changes.items()}}
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',side_effect=apply), \
             patch.object(commission,'capture',side_effect=capture):
            commission.envelope(object(),args,folder)
            report=json.loads(Path(folder,'envelope_report.json').read_text())
            self.assertEqual(report['recommended']['accel_rpm_s'],86250)
            self.assertEqual(report['best_worst_arrival_s'],0.200)
            self.assertEqual(report['best_worst_settling_s'],0.905964)

    def test_envelope_does_not_recommend_fast_arrival_without_stability(self):
        args=self.args();args.search='acceleration';args.ceiling=150;args.max_trials=1
        args.test_degrees=5;args.max_overshoot=2
        baseline=dict(revision=1,**{k:1000 for k in commission.KEYS});baseline['accel_rpm_s']=75000
        scores=iter((dict(passed=True,worst_arrival_s=0.275,worst_settling_s=0.9,worst_overshoot_deg=0.1),
                     dict(passed=False,worst_arrival_s=0.150,worst_settling_s=1e6,worst_overshoot_deg=0.1),
                     dict(passed=False,worst_arrival_s=0.150,worst_settling_s=1e6,worst_overshoot_deg=0.1)))
        def capture(board,args,folder,**kwargs):return [],dict(score=next(scores)),Path(folder,'trace.csv')
        with tempfile.TemporaryDirectory() as folder,contextlib.redirect_stdout(io.StringIO()), \
             patch.object(commission,'configuration',return_value=baseline), \
             patch.object(commission,'apply',return_value=baseline), \
             patch.object(commission,'capture',side_effect=capture):
            commission.envelope(object(),args,folder)
            report=json.loads(Path(folder,'envelope_report.json').read_text())
            self.assertEqual(report['recommended'],baseline)
            self.assertIn('failed',report['stopped_reason'])


class PositionScoreTests(unittest.TestCase):
    def rows(self,values,origin=0):
        return [dict(state=7,fault=0,us=(origin+i*1000)&0xffffffff,position=v,target=180)
                for i,v in enumerate(values)]

    def test_arrival_and_final_stability_have_one_confirmation_dwell(self):
        values=[170]*200+[180.8]*300+[180.4]*300+[180]*1201
        score=commission_model.score(self.rows(values))
        self.assertTrue(score['passed'])
        segment=score['segments'][0]
        self.assertAlmostEqual(segment['arrival_s'],0.2)
        self.assertAlmostEqual(segment['stable_start_s'],0.5)
        self.assertAlmostEqual(segment['settling_s'],1.0)
        self.assertEqual(score['score_schema'],2)

    def test_early_hold_followed_by_target_loss_does_not_pass(self):
        values=[170]*100+[180]*800+[182]*400+[180]*301
        score=commission_model.score(self.rows(values))
        self.assertFalse(score['passed'])
        self.assertIsNone(score['segments'][0]['settling_s'])
        self.assertAlmostEqual(score['segments'][0]['arrival_s'],1.3)

    def test_timestamp_wrap_preserves_arrival_and_confirmation(self):
        values=[170]*200+[180.8]*300+[180.4]*300+[180]*1201
        plain=commission_model.score(self.rows(values))
        wrapped=commission_model.score(self.rows(values,0xffffffff-400000))
        self.assertEqual(plain,wrapped)


class ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        np,_=commission_model.libraries()
        t=np.arange(0,6,0.001);rng=np.random.default_rng(321)
        u=np.repeat(rng.choice([-0.7,0.7],151),40)[:len(t)]
        cls.truth=[12,3,0.15,0.05,0.004,0.006]
        y=commission_model.simulate(cls.truth,t,u)
        cls.rows=[]
        for i in range(len(t)):
            row=dict.fromkeys(commission.FIELDS,0)
            row.update(sequence=i,us=i*1000,encoder_us=i*1000,config_revision=1,trial=1,
                       state=7,flags=14,velocity=float(y[i]),applied_v=float(u[i]),bus_v=24)
            cls.rows.append(row)
    def test_fit_recovers_known_model_and_holds_out_validation(self):
        result=commission_model.identify(self.rows)
        self.assertTrue(result['accepted'],result)
        self.assertLess(result['holdout_normalized_rmse'],0.1)
        self.assertAlmostEqual(result['parameters']['a'],12,delta=1.2)
        self.assertAlmostEqual(result['parameters']['b'],3,delta=0.3)
        self.assertTrue(result['candidates'])
        self.assertTrue(all(c['predicted_phase_margin_deg']>=50 for c in result['candidates']))
    def test_corrupt_capture_rejected_before_fit(self):
        with self.assertRaisesRegex(RuntimeError,'missing'):
            commission_model.identify(self.rows[:500]+self.rows[501:])
        stationary=[{**r,'velocity':0} for r in self.rows]
        with self.assertRaisesRegex(RuntimeError,'insufficient'):
            commission_model.identify(stationary)

    def test_measured_high_damping_capture_converges_and_produces_candidates(self):
        # Completed 2026-10-27 0.6-V / 50-ms capture. The original 80-evaluation
        # budget stopped early and wn<=24 excluded every positive-Kp candidate.
        fixture=Path(__file__).parent/'fixtures/voltage_06v_50ms.csv'
        result=commission_model.identify(commission_model.load(fixture))
        self.assertTrue(result['accepted'],result)
        self.assertTrue(result['optimizer']['converged'])
        self.assertLess(result['holdout_normalized_rmse'],0.3)
        self.assertTrue(result['candidates'])
        self.assertTrue(all(c['natural_frequency_rad_s']>24 for c in result['candidates']))
        self.assertTrue(all(c['predicted_phase_margin_deg']>=50 for c in result['candidates']))


if __name__=='__main__':unittest.main()
