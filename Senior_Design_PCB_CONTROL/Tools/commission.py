"""Measured commissioning CLI. 'demo' and 'analyze' never contact hardware.

Other commands use the new commissioning firmware. Capture/auto require a
calibrated/zeroed READY axis positioned at the selected centre, then physical S1.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import secrets
import socket
import struct
import threading
import time
import commission_model as model

KEYS=('velocity_kp','velocity_ki','position_kp','max_rpm','accel_rpm_s','run_volts')
FIELDS=('sequence','us','encoder_us','config_revision','trial','position','velocity','target',
        'trajectory','velocity_target','velocity_error','p_term','i_term','requested_v',
        'applied_v','bus_v','current_a','current_b','current_c','temperature','flags','state','fault')
ROW=struct.Struct('<5I15fIHH')


def fields(data,prefix):
    text=data.decode('ascii')
    if not text.startswith(prefix+' '):raise RuntimeError(f'Unexpected response: {text}')
    values={}
    for item in text.split()[1:]:
        key,value=item.split('=',1)
        try:values[key]=int(value)
        except ValueError:values[key]=value
    return values


class Board:
    def __init__(self,host='192.168.10.2',port=5005):
        self.socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.socket.connect((host,port));self.socket.settimeout(0.3)
        self.session=secrets.randbelow(0xffffffff)+1;self.sequence=0
        self.lock=threading.RLock();self.stop=threading.Event();self.thread=None;self.heartbeat_error=None

    def request(self,op,*args):
        with self.lock:
            self.sequence=(self.sequence+1)&0xffffffff
            packet=f'G1 {self.session} {self.sequence} {op}'
            if args:packet+=' '+' '.join(str(a) for a in args)
            self.socket.send(packet.encode())
            reply=self.socket.recv(2048)
            if reply.startswith(b'ERR'):raise RuntimeError(reply.decode())
            if op in ('STATUS','CONFIG','TESTINFO'):
                return fields(reply,{'STATUS':'S1','CONFIG':'C1','TESTINFO':'T1'}[op])
            if op=='TRACE':
                if len(reply)<24:raise RuntimeError('Truncated trace header')
                magic,version,trial,first,count,available=struct.unpack_from('<6I',reply)
                if magic!=0x31525443 or version!=1 or count>8 or len(reply)!=24+count*ROW.size:
                    raise RuntimeError('Trace schema/length mismatch')
                rows=[dict(zip(FIELDS,ROW.unpack_from(reply,24+i*ROW.size))) for i in range(count)]
                return dict(trial=trial,first=first,available=available,rows=rows)
            expected=f'OK G1 {self.session} {self.sequence}'.encode()
            if reply!=expected and not (op=='DISABLE' and reply==b'OK DISABLED'):
                raise RuntimeError(f'Unexpected acknowledgement: {reply!r}')
            return reply.decode()

    def start_heartbeat(self):
        if self.thread:return
        self.heartbeat_error=None;self.stop.clear()
        def worker():
            while not self.stop.wait(0.08):
                try:self.request('HEARTBEAT')
                except Exception as error:
                    self.heartbeat_error=str(error);return
        self.thread=threading.Thread(target=worker,daemon=True);self.thread.start()

    def finish(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=1)
        try:self.request('DISABLE')
        except (OSError,RuntimeError):pass
        self.socket.close()


def configuration(board):
    config=board.request('CONFIG')
    if config.get('schema')!=1:raise RuntimeError('Flash commissioning firmware (CONFIG schema=1 required)')
    if 'checksum' in config:
        checksum=2166136261
        for key in (*KEYS,'duty_span','voltage_limit'):
            for byte in struct.pack('<I',config[key]):checksum=((checksum^byte)*16777619)&0xffffffff
        if checksum!=config['checksum']:raise RuntimeError('Configuration checksum mismatch')
    return config


def human_config(config):return {key:config[key]/1000 for key in KEYS}


def completed_capture(path,active=None):
    path=Path(path)
    context=json.loads(path.with_suffix('.json').read_text())
    if context.get('completed') is not True or context.get('error'):
        raise RuntimeError('Capture was incomplete/faulted; it cannot seed automatic tuning')
    if context.get('mode') not in ('voltage','prbs'):
        raise RuntimeError('Identification requires a voltage/prbs excitation capture')
    if active is not None:
        recorded=context.get('config',{})
        for key in (*KEYS,'axis','source','current_pi','schema','build','duty_span','voltage_limit'):
            if recorded.get(key)!=active.get(key):
                raise RuntimeError(f'Capture configuration differs from board: {key}; collect a new identification capture')
        if active.get('axis')==1:
            for key in ('min_mdeg','max_mdeg','encoder_sign'):
                if recorded.get(key)!=active.get(key):
                    raise RuntimeError(f'Pan capture travel/polarity differs from board: {key}')
    return model.load(path),context


def apply(board,changes):
    for key,value in changes.items():
        if key not in KEYS or not math.isfinite(value) or value<0:raise ValueError(f'Invalid parameter {key}')
        board.request('SET',KEYS.index(key),round(value*1000))
    actual=configuration(board)
    for key,value in changes.items():
        if actual[key]!=round(value*1000):raise RuntimeError(f'Parameter readback mismatch: {key}')
    return actual


def capture(board,args,folder,mode=None,amplitude=None,label='trial',period_ms=None,duration=None):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    config=configuration(board);status=board.request('STATUS')
    if not (status['axis']==args.axis and status['source']==1 and status['enabled'] and
            status['calibrated'] and status['zero'] and status['state']==5 and not status['fault']):
        raise RuntimeError('Capture requires selected Ethernet axis, motion enabled, calibrated/zeroed READY')
    if config['current_pi']:raise RuntimeError('This tuner is for voltage mode')
    if args.centre-args.radius<config.get('min_mdeg',2000)/1000 or \
       args.centre+args.radius>config.get('max_mdeg',358000)/1000:
        raise RuntimeError('Experiment window exceeds this motor\'s configured command limits; choose centre/radius inside CONFIG min/max')
    modes={'position':0,'voltage':1,'velocity':2,'prbs':3}
    selected=mode or args.mode
    amp=amplitude if amplitude is not None else args.amplitude
    period_ms=period_ms if period_ms is not None else (((args.period_ms or 6000) if mode is None else 6000) if selected=='position' else (args.period_ms or 100))
    duration=duration if duration is not None else (max(args.duration,12) if selected=='position' else args.duration)
    if abs(status['position_mdeg']/1000-args.centre)>args.radius:
        raise RuntimeError('Position shaft at test centre using position_client.py first; TEST never redefines zero')
    stem=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')+'_'+label
    path=folder/(stem+'.csv');metadata=folder/(stem+'.json')
    result=dict(config=config,start_status=status,mode=selected,amplitude=amp,
                period_ms=period_ms,duration_s=duration,centre=args.centre,radius=args.radius,
                completed=False,error=None)
    rows=[];next_sequence=0;trial=None
    try:
        board.request('TEST',modes[selected],round(amp*1000),period_ms,round(duration*1000),
                      round(args.centre*1000),round(args.radius*1000))
        board.start_heartbeat()
        print(f'{label}: {selected}, amplitude={amp:g}; first trial needs S1 once. S2/Ctrl-C cancels batch.',flush=True)
        deadline=time.monotonic()+args.arm_timeout+duration
        with path.open('w',newline='') as stream:
            writer=csv.DictWriter(stream,fieldnames=FIELDS);writer.writeheader()
            while time.monotonic()<deadline:
                packet=board.request('TRACE',next_sequence)
                if trial is None:trial=packet['trial']
                if packet['trial']!=trial or packet['first']!=next_sequence:
                    raise RuntimeError('Trace overwritten or trial changed; identification invalid')
                for row in packet['rows']:
                    if row['sequence']!=next_sequence:raise RuntimeError('Trace sequence gap')
                    if row['config_revision']!=config['revision']:raise RuntimeError('Configuration changed during trial')
                    writer.writerow(row);rows.append(row);next_sequence+=1
                stream.flush()
                if packet['available']>next_sequence:continue
                info=board.request('TESTINFO')
                status=board.request('STATUS')
                if status['fault']:raise RuntimeError(f'Controller fault={status["fault"]}: {status}')
                if info['aborted']:raise RuntimeError('Trial aborted (operator stop/travel window/disconnect); no automatic rearm')
                if board.heartbeat_error:raise RuntimeError(f'Heartbeat stopped: {board.heartbeat_error}; {status}')
                if info['complete'] and info['next']==next_sequence:
                    result['completed']=True;break
                time.sleep(0.004)
            else:raise TimeoutError('No completed trial before arm/duration timeout')
    except BaseException as error:
        result['error']=f'{type(error).__name__}: {error}';raise
    finally:
        result['score']=model.score(rows)
        metadata.write_text(json.dumps(result,indent=2)+'\n')
        model.svg(rows,path.with_suffix('.svg'))
        print(f'Saved {path} ({len(rows)} MCU samples)',flush=True)
    return rows,result,path


def auto(board,args,folder):
    baseline=configuration(board)
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    if getattr(args,'trace',None):
        rows,meta=completed_capture(args.trace,baseline)
        model_path=folder/'identification.model.json'
        print(f'Using completed identification capture: {args.trace}; no new voltage excitation.',flush=True)
    else:
        rows,meta,path=capture(board,args,folder,mode='prbs',label='identify')
        model_path=path.with_suffix('.model.json')
    identified=model.identify(rows)
    identified['source_trace']=str(args.trace) if getattr(args,'trace',None) else str(path)
    model_path.write_text(json.dumps(identified,indent=2)+'\n')
    if not identified['accepted'] or not identified['candidates']:
        raise RuntimeError('Identification inconclusive; no candidate applied. See model report.')
    best=None;trials=[];position_trials=[];report={}
    try:
        # Fixed slow profile during loop tuning: speed/acceleration search is
        # deliberately separate from changing controller gains.
        _,reference,_=capture(board,args,folder,mode='position',amplitude=args.test_degrees,label='baseline')
        baseline_score=reference['score']
        for number,candidate in enumerate(identified['candidates'][:args.max_trials],1):
            changes={key:candidate[key] for key in ('velocity_kp','velocity_ki')}
            active=apply(board,changes)
            velocity_rows,_,_=capture(board,args,folder,mode='velocity',amplitude=3,
                period_ms=800,duration=6.4,label=f'velocity_{number}')
            score=model.score_velocity(velocity_rows)
            trials.append(dict(candidate=candidate,config=active,score=score))
            print(f'Velocity candidate {number}: {score}',flush=True)
            if score['passed']:
                if best is None or score['rmse_rad_s']<best['score']['rmse_rad_s']:
                    best=trials[-1]
        report=dict(baseline=baseline,baseline_score=baseline_score,model=identified,candidates=trials,best=best,
                    objective='Minimize worst time to enter and remain within +/-1 degree; stability/overshoot must qualify',
                    note='Candidates tested locally at one pose/load. Cross-pose/repeated/thermal qualification remains required.')
        if best is not None:
            apply(board,{key:best['candidate'][key] for key in ('velocity_kp','velocity_ki')})
            position_best=(dict(config=baseline,score=baseline_score) if baseline_score['passed'] and
                           baseline_score['worst_overshoot_deg']<=args.max_overshoot else None)
            for index,factor in enumerate((0.75,1.0,1.25),1):
                kp=best['candidate']['position_kp']*factor
                active=apply(board,dict(position_kp=kp))
                _,trial,_=capture(board,args,folder,mode='position',amplitude=args.test_degrees,label=f'position_{index}')
                score=trial['score'];position_trials.append(dict(config=active,score=score))
                if score['passed'] and score['worst_overshoot_deg']<=args.max_overshoot:
                    if position_best is None or model.performance_key(score)<model.performance_key(position_best['score']):
                        position_best=position_trials[-1]
            report['position_candidates']=position_trials
            if position_best is None:
                raise RuntimeError('No position candidate met tolerance/overshoot; no recommendation')
            active=apply(board,{key:position_best['config'][key]/1000 for key in KEYS})
            # Holdout amplitude, two repeated firmware-timed trials, same envelope.
            holdouts=[]
            for repeat in range(2):
                _,trial,_=capture(board,args,folder,mode='position',amplitude=args.test_degrees*0.7,label=f'holdout_{repeat+1}')
                holdouts.append(trial['score'])
            report['holdouts']=holdouts
            report['recommendation_verified']=all(s['passed'] and s['worst_overshoot_deg']<=args.max_overshoot for s in holdouts)
            report['recommended_config']=active if report['recommendation_verified'] else None
        Path(folder,'auto_report.json').write_text(json.dumps(report,indent=2)+'\n')
        print('Report:',Path(folder,'auto_report.json'),flush=True)
    except BaseException as error:
        report.update(baseline=baseline,model=identified,candidates=trials,
                      position_candidates=position_trials,error=str(error),recommendation_verified=False)
        Path(folder,'auto_report.json').write_text(json.dumps(report,indent=2)+'\n')
        raise
    finally:
        # RAM trial search never silently installs the winner as the new default.
        # On a latched fault SET is intentionally rejected; DISABLE still follows.
        try:apply(board,{key:baseline[key]/1000 for key in KEYS})
        except (OSError,RuntimeError):print('Baseline restore not acknowledged; reset restores compiled defaults.',flush=True)
    if report.get('recommended_config'):
        print('Recommended (not automatically persisted):',human_config(report['recommended_config']),flush=True)
    else:print('No validated improvement recommended; baseline retained.',flush=True)


def demo(folder):
    np,_=model.libraries()
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    t=np.arange(0,8,0.001)
    rng=np.random.default_rng(123)
    signs=rng.choice([-0.7,0.7],101)
    u=np.repeat(np.column_stack((signs,-signs)).ravel(),40)[:len(t)]
    truth=[12,3,0.15,0.05,0.004,0.006]
    speed=model.simulate(truth,t,u)
    position=180+np.cumsum(speed)*0.001*180/np.pi
    rows=[]
    for i in range(len(t)):
        row=dict.fromkeys(FIELDS,0)
        row.update(sequence=i,us=i*1000,encoder_us=i*1000,config_revision=1,trial=1,
                   state=7,flags=14,position=float(position[i]),velocity=float(speed[i]),
                   target=180,trajectory=180,applied_v=float(u[i]),requested_v=float(u[i]),bus_v=24,temperature=25)
        rows.append(row)
    path=folder/'synthetic_identification.csv'
    with path.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=FIELDS);writer.writeheader();writer.writerows(rows)
    identified=model.identify(rows)
    identified['synthetic_truth']=truth
    path.with_suffix('.json').write_text(json.dumps(identified,indent=2)+'\n')
    model.svg(rows,path.with_suffix('.svg'))
    print(json.dumps(identified,indent=2))
    if not identified['accepted']:raise RuntimeError('Synthetic identification self-test failed')


def verify(board,args,folder):
    """Explicit position validation; a rejected velocity score stays rejected.

    Compare specified gains against the baseline without repeating voltage ID
    or velocity screening. Only measured position/holdout passes can produce a
    recommendation; no change is persisted.
    """
    changes={key:getattr(args,key,None) for key in KEYS[:3]
             if getattr(args,key,None) is not None}
    if not changes:raise ValueError('verify requires --velocity-kp, --velocity-ki or --position-kp')
    if any(not math.isfinite(value) or value<0 for value in changes.values()):
        raise ValueError('verify gains must be finite and nonnegative')
    if any(getattr(args,key,None) is not None for key in KEYS[3:]):
        raise ValueError('verify compares loop gains only; leave speed, acceleration and voltage limits unchanged')
    baseline=configuration(board);folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    report=dict(baseline=baseline,requested_changes=changes,holdouts=[],
                objective='Minimize worst time to enter and remain within +/-1 degree; stability/overshoot must qualify',
                recommendation_verified=False,recommended_config=None,baseline_restored=False,
                note='Explicit local position experiment. Does not override or relabel a rejected velocity test.')
    def qualified(score):
        return bool(score['passed'] and score['worst_overshoot_deg']<=args.max_overshoot)
    try:
        _,reference,path=capture(board,args,folder,mode='position',amplitude=args.test_degrees,label='baseline')
        report.update(baseline_score=reference['score'],baseline_trace=str(path))
        active=apply(board,changes);report['candidate_config']=active
        _,trial,path=capture(board,args,folder,mode='position',amplitude=args.test_degrees,label='candidate')
        score=trial['score'];report.update(candidate_score=score,candidate_trace=str(path))
        if qualified(score):
            for repeat in range(2):
                _,held,path=capture(board,args,folder,mode='position',amplitude=args.test_degrees*0.7,
                                    label=f'holdout_{repeat+1}')
                report['holdouts'].append(dict(score=held['score'],trace=str(path)))
            baseline_score=reference['score']
            improved=not qualified(baseline_score) or model.performance_key(score)<model.performance_key(baseline_score)
            report['improved_over_baseline']=bool(improved)
            report['recommendation_verified']=bool(improved and all(qualified(t['score']) for t in report['holdouts']))
            if report['recommendation_verified']:report['recommended_config']=active
        else:
            report['reason']='Candidate did not meet position stability/overshoot; no holdouts or recommendation'
    except BaseException as error:
        report.update(error=f'{type(error).__name__}: {error}',recommendation_verified=False,recommended_config=None)
        raise
    finally:
        try:
            apply(board,{key:baseline[key]/1000 for key in KEYS})
            report['baseline_restored']=True
        except (OSError,RuntimeError) as error:
            report['restore_error']=str(error)
            print('Baseline restore not acknowledged; reset restores compiled defaults.',flush=True)
        (folder/'verify_report.json').write_text(json.dumps(report,indent=2)+'\n')
        print('Report:',folder/'verify_report.json',flush=True)
    print('Candidate position score:',report['candidate_score'],flush=True)
    if report['recommended_config']:
        print('Recommended after local position holdouts (not persisted):',human_config(report['recommended_config']),flush=True)
    else:print('No verified position improvement recommended; baseline retained.',flush=True)


def envelope(board,args,folder):
    if args.ceiling is None:raise ValueError('Envelope search requires an explicit --ceiling')
    key='accel_rpm_s' if args.search=='acceleration' else 'max_rpm'
    baseline=configuration(board);value=baseline[key]/1000
    if not math.isfinite(args.ceiling) or args.ceiling<=value:
        raise ValueError('--ceiling must be finite and above the current value')
    report=dict(baseline=baseline,search=args.search,ceiling=args.ceiling,trials=[],recommended=None,
                objective='Minimize worst time to enter and remain within +/-1 degree; stability/overshoot must qualify',
                baseline_restored=False,
                warning='Short-trial operating envelope only; not a thermal/continuous or physical-maximum qualification.')
    try:
        _,reference,path=capture(board,args,folder,mode='position',amplitude=args.test_degrees,label='envelope_baseline')
        reference_score=reference['score']
        report.update(baseline_score=reference_score,baseline_trace=str(path))
        if not reference_score['passed'] or reference_score['worst_overshoot_deg']>args.max_overshoot:
            raise RuntimeError('Baseline position trial did not qualify; resolve this before increasing trajectory limits')
        report['recommended']=baseline;report['best_worst_settling_s']=reference_score['worst_settling_s']
        report['best_worst_arrival_s']=model.performance_key(reference_score)[0]
        for trial in range(args.max_trials):
            value=min(args.ceiling,value*1.15)
            active=apply(board,{key:value});scores=[];peaks=[]
            for repeat in range(2):
                rows,meta,_=capture(board,args,folder,mode='position',amplitude=args.test_degrees,
                                    label=f'envelope_{trial+1}_{repeat+1}')
                scores.append(meta['score'])
                peaks.append(max((abs(float(r['velocity']))*60/(2*math.pi) for r in rows),default=0))
            record=dict(config=active,scores=scores,peak_rpm=max(peaks));report['trials'].append(record)
            if not all(s['passed'] and s['worst_overshoot_deg']<=args.max_overshoot for s in scores):
                report['stopped_reason']='Candidate failed position/settling score';break
            report['highest_passed']=active
            worst=max(s['worst_settling_s'] for s in scores)
            arrival=max(model.performance_key(s)[0] for s in scores)
            record['worst_settling_s']=worst
            record['worst_arrival_s']=arrival
            if (arrival,worst)<(report['best_worst_arrival_s'],report['best_worst_settling_s']):
                report['recommended']=active;report['best_worst_settling_s']=worst
                report['best_worst_arrival_s']=arrival
            if args.search=='speed' and max(peaks)<0.9*value:
                report['stopped_reason']='Move did not approach speed ceiling; raising it further would not establish a faster usable speed';break
            if value>=args.ceiling:break
    except BaseException as error:
        report['error']=str(error);raise
    finally:
        Path(folder).mkdir(parents=True,exist_ok=True)
        try:
            apply(board,{key:baseline[key]/1000});report['baseline_restored']=True
        except (OSError,RuntimeError) as error:
            report['restore_error']=str(error)
            print('Restore not acknowledged; reset restores compiled defaults.')
        Path(folder,'envelope_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('info','set','capture','analyze','auto','verify','envelope','demo'))
    parser.add_argument('--host',default='192.168.10.2');parser.add_argument('--port',type=int,default=5005)
    parser.add_argument('--axis',type=int,choices=(0,1),default=0)
    parser.add_argument('--out',type=Path,default=Path('logs/commission')/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    parser.add_argument('--trace',type=Path,help='Analyze this CSV, or reuse a completed voltage capture for auto without repeating excitation')
    parser.add_argument('--mode',choices=('position','voltage','velocity','prbs'),default='prbs')
    parser.add_argument('--amplitude',type=float,default=0.3,help='V for voltage/prbs; RPM velocity; degrees position')
    parser.add_argument('--centre',type=float,default=180);parser.add_argument('--radius',type=float,default=30)
    parser.add_argument('--period-ms',type=int,default=None);parser.add_argument('--duration',type=float,default=8)
    parser.add_argument('--arm-timeout',type=float,default=60)
    parser.add_argument('--test-degrees',type=float,default=10)
    parser.add_argument('--max-trials',type=int,default=4)
    parser.add_argument('--max-overshoot',type=float,default=2)
    parser.add_argument('--search',choices=('acceleration','speed'),default='acceleration')
    parser.add_argument('--ceiling',type=float,help='Explicit maximum RPM/s or RPM for envelope search')
    for key in KEYS:parser.add_argument('--'+key.replace('_','-'),type=float)
    args=parser.parse_args()
    values=[args.amplitude,args.centre,args.radius,args.duration,args.arm_timeout,args.test_degrees,args.max_overshoot]
    if not all(math.isfinite(v) for v in values) or not (args.amplitude>0 and 0<args.duration<=30 and
        args.arm_timeout>0 and (args.period_ms is None or 20<=args.period_ms<=10000) and 0<args.radius<=90 and
        args.centre-args.radius>=2 and args.centre+args.radius<=358 and
        0<args.test_degrees<=args.radius and 1<=args.max_trials<=8 and args.max_overshoot>=0):
        parser.error('Invalid experiment envelope/duration/amplitude')
    if args.command=='demo':demo(args.out);return
    if args.command=='analyze':
        if args.trace is None:parser.error('--trace is required')
        metadata=args.trace.with_suffix('.json')
        if metadata.exists():
            context=json.loads(metadata.read_text())
            if context.get('completed') is False or context.get('error'):
                raise RuntimeError('Capture was incomplete/faulted; inspect its metadata before identification')
        rows=model.load(args.trace);report=model.identify(rows)
        args.out.mkdir(parents=True,exist_ok=True)
        (args.out/'model.json').write_text(json.dumps(report,indent=2)+'\n')
        model.svg(rows,args.out/'trace.svg');print(json.dumps(report,indent=2));return
    if args.command in ('capture','auto','verify','envelope'):
        model.libraries()
        try:import matplotlib  # Fail before contacting the board if plotting is unavailable.
        except ImportError as error:
            raise RuntimeError('Install dependencies: py -3 -m pip install -r Tools/requirements-tuning.txt') from error
    board=Board(args.host,args.port)
    try:
        if args.command=='info':
            config=configuration(board);print(config);print('Human units:',human_config(config))
            print(board.request('STATUS'));print(board.request('TESTINFO'))
        elif args.command=='set':
            changes={key:getattr(args,key) for key in KEYS if getattr(args,key) is not None}
            if not changes:parser.error('Provide one or more gain/limit options')
            config=apply(board,changes)
            # SET only succeeds while inhibited. Release this client's lease
            # immediately so the following test can acquire its own session.
            board.request('DISABLE')
            print(config);print('Human units:',human_config(config))
        elif args.command=='capture':capture(board,args,args.out)
        elif args.command=='auto':auto(board,args,args.out)
        elif args.command=='verify':verify(board,args,args.out)
        elif args.command=='envelope':envelope(board,args,args.out)
    finally:
        if args.command in ('capture','auto','verify','envelope'):board.finish()
        else:board.socket.close()


if __name__=='__main__':
    try:main()
    except KeyboardInterrupt:print('Stopped by operator.');raise SystemExit(130)
    except (RuntimeError,OSError,ValueError,TimeoutError) as error:print('FAIL:',error);raise SystemExit(1)
