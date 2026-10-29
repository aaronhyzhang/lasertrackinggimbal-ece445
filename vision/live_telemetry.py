"""Read-only dual-axis terminal dashboard and CSV recorder. Never arms, sends
POSITION/HEARTBEAT, changes ownership, renews a lease, or sends DISABLE on exit.
Automatically recorded by vision.run --ethernet; also runs as a standalone
dashboard with python -m vision.live_telemetry. Requires DIAG/HEALTH firmware.
Python standard library only.
"""
import argparse
from collections import deque
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import socket
import shutil
import sys
import textwrap
import threading
import time

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = 'vision'

from Tools.position_client import Client

STATES = 'OFF WAKE_CAL ALIGN SWEEP RETURN READY WAKE_RUN RUNNING FAULT'.split()
FAULTS = ('OK DRIVER FEEDBACK ZERO_TIMEOUT CALIBRATION CURRENT CURRENT_SUM TRAVEL '
          'FOLLOWING COMMAND_TIMEOUT SUPPLY TEMPERATURE TIMING FOREGROUND INTERNAL').split()
BOARD_FAULTS = 'NONE DRIVER ADC FOREGROUND CONTROL_TIMING INTERNAL DRIVER_CONFIG CURRENT_CALIBRATION'.split()
ENCODER = 'ACCEPTED TRANSPORT STALE MISSING_MAGNET WEAK_FIELD SAMPLE_GAP JUMP'.split()
DRIVER_TRIPS = 'NONE PWM_PERMISSION START_NFAULT_LOW NFAULT_EDGE TIM1_BREAK'.split()
DIAG_KEYS = ('axis ms state fault cal zero settled weak rx_mdeg target_mdeg traj_mdeg pos_mdeg '
             'v_mrpm tv_mrpm vr_mrpm a_mrpm_s ta_mrpm_s rx_n rx_age_ms rx_dt_ms rx_min_ms rx_max_ms '
             'lease_age_ms valid vq_mv requested_mv limit_mv sat duty_a duty_b duty_c enc_age_us cur_age_us '
             'fg_age_ms tick_age_us max_tick_us max_fast_us max_mrpm accel_mrpm_s').split()
HEALTH_KEYS = ('axis ms board_fault driver_good drv_trip awake configured pwm stop read_mask failed_reg '
               'r0 r1 r2 r3 r4 r5 r6 trip trip_state trip_ms trip_enc trip_enc_age_us trip_cur_age_us '
               'enc_reject enc_ok analog_ok cur_ok window zero_ok temp_ok adc_failed adc_fresh adc0 adc1 adc2 '
               'bus_mv temp_mc ia_ma ib_ma ic_ma sum_ma phase_trip net mbps duplex phy_mode tx rx syntax_err '
               'cmd_err owner_err seq_err reject_ms reject_axis').split()
HOST_KEYS = ('snapshot_perf_s feedback_hz feedback_age_ms frame_age_ms tracking '
             'submitted_observations replaced_observations pending_observation '
             'pan_error_deg tilt_error_deg tilt_measured_deg pan_measured_deg '
             'tilt_target_deg pan_target_deg tilt_frame_to_send_ms pan_frame_to_send_ms '
             'tilt_position_rtt_ms pan_position_rtt_ms').split()


class TelemetryRecorder:
    """Record alongside vision using independent read-only UDP clients."""
    def __init__(self, settings, log_path, logger, host_snapshot=None, client_factory=Client):
        self.base = Path(log_path).with_name(Path(log_path).stem + '-telemetry')
        self.csv_path = Path(str(self.base) + '.csv')
        self.logger, self.host_snapshot, self.client_factory = logger, host_snapshot, client_factory
        self.args = argparse.Namespace(host=settings.host, axis='both', tilt_port=settings.tilt_port,
                                       pan_port=settings.pan_port, rate_hz=20., health_hz=2.,
                                       duration=None, out=self.base.parent, plain=True, full=False)
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        if self._thread is not None:
            raise RuntimeError('Telemetry recorder already started')
        self._thread = threading.Thread(target=self._run, name='gimbal-telemetry', daemon=True)
        self._thread.start()
        return self

    def _run(self):
        try:
            record(self.args, stop=self._stop, runtime_log=self.logger, base=self.base,
                   host_snapshot=self.host_snapshot, client_factory=self.client_factory)
        except Exception:
            self.logger.exception('Telemetry recorder failed; tracking continues')

    def close(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
            if self._thread.is_alive():
                self.logger.warning('Telemetry recorder did not finish within two seconds')


def name(names, value):
    return names[value] if isinstance(value, int) and 0 <= value < len(names) else str(value)


def human(data, key):
    value = data.get(key, -2147483648)
    return float('nan') if value == -2147483648 else value / 1000


def validate(data, axis, keys):
    if data.get('supported') == 0:
        raise ValueError('DIAG/HEALTH requires the dual-axis diagnostic firmware')
    if data.get('axis') != axis or any(not isinstance(data.get(k), int) for k in keys):
        raise ValueError('Incomplete or wrong-axis diagnostic response; check firmware version')
    return data


def metrics(samples):
    if not samples:
        return {}
    def rms(key):
        return math.sqrt(sum(s[key] ** 2 for s in samples) / len(samples))
    return {'target_rms': rms('target_error'), 'planner_rms': rms('planner_error'),
            'motor_rms': rms('motor_error'),
            'saturated_fraction': sum(s['sat'] for s in samples) / len(samples)}


def observation(d):
    return {'target_error': human(d, 'rx_mdeg') - human(d, 'pos_mdeg'),
            'planner_error': human(d, 'target_mdeg') - human(d, 'traj_mdeg'),
            'motor_error': human(d, 'traj_mdeg') - human(d, 'pos_mdeg'), 'sat': bool(d['sat'])}


def dashboard(data, health, windows, starts, errors, rtts, health_ages):
    lines = ['GIMBAL TELEMETRY | READ ONLY | Ctrl-C closes viewer; motor command process continues',
             'Degrees are mechanical / relative to calibration zero. Accel is a noisy ~20 ms speed derivative.',
             'RX = last accepted Ethernet position. Trajectory = board motion reference. No command lease renewal.', '']
    for axis in sorted(set(data) | set(errors)):
        label = 'TILT / J3 / 5005' if axis == 0 else 'PAN / J4 / 5006'
        lines.append(label + ('  [COMMUNICATION ERROR: ' + errors[axis] + ']' if axis in errors else ''))
        if axis not in data:
            lines.append('  Waiting for diagnostic firmware response...')
            continue
        d, h = data[axis], health.get(axis, {})
        o, m = observation(d), metrics(windows[axis])
        lines += [f"  State {name(STATES,d['state'])}  fault {name(FAULTS,d['fault'])}  "
                  f"cal={d['cal']} zero={d['zero']} settled={d['settled']} weak={d['weak']}",
                  f"  Position deg   RX {human(d,'rx_mdeg'):9.3f}  effective {human(d,'target_mdeg'):9.3f}  "
                  f"trajectory {human(d,'traj_mdeg'):9.3f}  measured {human(d,'pos_mdeg'):9.3f}",
                  f"  Error deg      RX-measured {o['target_error']:8.3f}  target-trajectory {o['planner_error']:8.3f}  "
                  f"trajectory-measured {o['motor_error']:8.3f}  from viewer start {human(d,'pos_mdeg')-starts[axis]:8.3f}",
                  f"  Speed RPM      trajectory {human(d,'tv_mrpm'):8.2f}  loop demand {human(d,'vr_mrpm'):8.2f}  "
                  f"measured {human(d,'v_mrpm'):8.2f}  limit {human(d,'max_mrpm'):7.2f}",
                  f"  Accel RPM/s    trajectory {human(d,'ta_mrpm_s'):8.2f}  measured estimate {human(d,'a_mrpm_s'):8.2f}  "
                  f"selected limit {human(d,'accel_mrpm_s'):7.2f}",
                  f"  Effort V       PI requested {human(d,'requested_mv'):7.3f}  applied Vq {human(d,'vq_mv'):7.3f}  "
                  f"limit {human(d,'limit_mv'):6.3f}  saturated={d['sat']}  "
                  f"duty A/B/C {d['duty_a']/10:.1f}/{d['duty_b']/10:.1f}/{d['duty_c']/10:.1f}%",
                  f"  RX commands    count={d['rx_n']} age={d['rx_age_ms']}ms  last interval={d['rx_dt_ms']}ms  "
                  f"min/max={d['rx_min_ms']}/{d['rx_max_ms']}ms  lease age={d['lease_age_ms']}ms valid={d['valid']}",
                  f"  Timing         encoder={d['enc_age_us']}us current={d['cur_age_us']}us foreground={d['fg_age_ms']}ms  "
                  f"max control/fast={d['max_tick_us']}/{d['max_fast_us']}us  poll RTT={rtts.get(axis,0)*1000:.1f}ms",
                  f"  Last <=10s RMS target/planner/motor {m.get('target_rms',0):.2f}/"
                  f"{m.get('planner_rms',0):.2f}/{m.get('motor_rms',0):.2f} deg; "
                  f"saturation {100*m.get('saturated_fraction',0):.1f}%"]
        if h:
            regs = ' '.join(f'{h[f"r{i}"]:03X}' for i in range(7))
            lines += [f"  Health age {health_ages.get(axis,0):.1f}s | board fault {name(BOARD_FAULTS,h['board_fault'])}  "
                      f"driver trip {name(DRIVER_TRIPS,h['drv_trip'])}  awake/config/PWM={h['awake']}/{h['configured']}/{h['pwm']}  STOP={h['stop']}",
                      f"  Feedback       encoder={h['enc_ok']} ({name(ENCODER,h['enc_reject'])}) analog={h['analog_ok']} "
                      f"current={h['cur_ok']} window={h['window']} zero={h['zero_ok']} temp={h['temp_ok']}  "
                      f"ADC errors={h['adc0']}/{h['adc1']}/{h['adc2']} failed mask=0x{h['adc_failed']:X}",
                      f"  Supply/temp    {human(h,'bus_mv'):.2f}V / {human(h,'temp_mc'):.1f}C  "
                      f"currents A/B/C={human(h,'ia_ma'):.3f}/{human(h,'ib_ma'):.3f}/{human(h,'ic_ma'):.3f}A "
                      f"[not qualified; phase trip enabled={h['phase_trip']}]",
                      f"  Driver cached registers 0..6: {regs}  read mask=0x{h['read_mask']:X} failed register={h['failed_reg']}",
                      f"  Ethernet       {h['mbps']}Mb/s {'full' if h['duplex'] else 'half'} duplex  "
                      f"RX/TX={h['rx']}/{h['tx']} errors syntax/app/owner/sequence="
                      f"{h['syntax_err']}/{h['cmd_err']}/{h['owner_err']}/{h['seq_err']}"]
            if h['trip']:
                lines.append(f"  FROZEN TRIP    t={h['trip_ms']}ms in {name(STATES,h['trip_state'])}  "
                             f"encoder={name(ENCODER,h['trip_enc'])} ages enc/current="
                             f"{h['trip_enc_age_us']}/{h['trip_cur_age_us']}us")
        lines.append('')
    lines += ['Interpretation: target-trajectory -> planner/limits; trajectory-measured -> control/load/effort.',
              'Large RX age despite heartbeats -> vision/command publisher freshness. Polling is not a 1kHz trace.']
    return '\n'.join(lines)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='192.168.10.2')
    p.add_argument('--axis', choices=('both','0','1'), default='both')
    p.add_argument('--tilt-port', type=int, default=5005)
    p.add_argument('--pan-port', type=int, default=5006)
    p.add_argument('--rate-hz', type=float, default=20)
    p.add_argument('--health-hz', type=float, default=2)
    p.add_argument('--duration', type=float, help='Seconds; default runs until Ctrl-C')
    p.add_argument('--out', type=Path, default=Path(__file__).with_name('logs') / 'live_telemetry')
    p.add_argument('--plain', action='store_true', help='One compact line per axis each second; no screen redraw')
    p.add_argument('--full', action='store_true', help='Full dashboard even in a short terminal; otherwise fit a summary')
    args = p.parse_args(argv)
    if (not math.isfinite(args.rate_hz) or not 1 <= args.rate_hz <= 50 or
        not math.isfinite(args.health_hz) or not .2 <= args.health_hz <= args.rate_hz or
        (args.duration is not None and (not math.isfinite(args.duration) or args.duration <= 0))):
        p.error('rate 1..50 Hz, health .2..rate Hz, duration positive and finite')
    return record(args)


def record(args, *, stop=None, runtime_log=None, base=None, host_snapshot=None, client_factory=Client):
    """Shared terminal/background recorder. Never use motion-owner sockets."""
    stop = stop if stop is not None else threading.Event()
    axes = (0,1) if args.axis == 'both' else (int(args.axis),)
    clients = {}
    args.out.mkdir(parents=True, exist_ok=True)
    base = base or args.out / ('telemetry_' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    csv_path, metadata_path = Path(str(base) + '.csv'), Path(str(base) + '.json')
    data, health, errors, starts, rtts, health_times = {}, {}, {}, {}, {}, {}
    windows = {i: deque(maxlen=500) for i in axes}
    sample_times = {i: deque(maxlen=500) for i in axes}
    counts = {i: 0 for i in axes}
    metadata = {'read_only': True, 'settings': {k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
                'config': {}, 'errors': [], 'counts': counts}
    start = time.monotonic(); next_sample = next_health = next_screen = start
    columns = (['host_s','host_perf_s','host_utc','rtt_ms','health_host_age_s',
                'target_error_deg','planner_error_deg','motor_error_deg'] +
               ['d_'+k for k in DIAG_KEYS] + ['h_'+k for k in HEALTH_KEYS] + ['host_'+k for k in HOST_KEYS])
    # Windows console ANSI, without packages. If unavailable use plain lines.
    plain = args.plain or not sys.stdout.isatty()
    if not plain and sys.platform == 'win32':
        import ctypes
        kernel = ctypes.windll.kernel32
        handle = kernel.GetStdHandle(-11)
        mode = ctypes.c_ulong()
        plain = not (kernel.GetConsoleMode(handle,ctypes.byref(mode)) and
                     kernel.SetConsoleMode(handle,mode.value | 4))
    exit_code = 0
    if runtime_log:
        runtime_log.info('Telemetry CSV: %s; metadata: %s; DIAG %.1f Hz/axis, HEALTH %.1f Hz/axis',
                         csv_path, metadata_path, args.rate_hz, args.health_hz)
    else:
        print(f'Read-only logging: {csv_path} (Ctrl-C exits without stopping motors)', flush=True)
    try:
        for i in axes:
            clients[i] = client_factory(args.host, (args.tilt_port,args.pan_port)[i], timeout=.05)
        for i,c in clients.items():
            metadata['config'][i] = c.request('CONFIG')
            validate(c.request('DIAG'),i,DIAG_KEYS)
            validate(c.request('HEALTH'),i,HEALTH_KEYS)
        with csv_path.open('x',newline='',encoding='utf-8') as f:
            writer = csv.DictWriter(f,fieldnames=columns);writer.writeheader()
            while not stop.is_set() and (args.duration is None or time.monotonic()-start < args.duration):
                now = time.monotonic()
                if now < next_sample:
                    stop.wait(min(next_sample-now,.02));continue
                health_due = now >= next_health
                for i,c in clients.items():
                    if stop.is_set():
                        break
                    try:
                        begin=time.monotonic()
                        d=validate(c.request('DIAG'),i,DIAG_KEYS)
                        rtts[i]=time.monotonic()-begin
                        if health_due:
                            health[i]=validate(c.request('HEALTH'),i,HEALTH_KEYS)
                            health_times[i]=time.monotonic()
                        errors.pop(i,None)
                        if i in data and d['ms'] < data[i]['ms'] and data[i]['ms']-d['ms'] < 0x80000000:
                            windows[i].clear();sample_times[i].clear();starts.pop(i,None)
                        data[i]=d;starts.setdefault(i,human(d,'pos_mdeg'))
                        stamp=time.monotonic();windows[i].append(observation(d));sample_times[i].append(stamp)
                        while sample_times[i] and stamp-sample_times[i][0] > 10:
                            sample_times[i].popleft();windows[i].popleft()
                        counts[i]+=1
                        row={'host_s':stamp-start,'host_perf_s':time.perf_counter(),
                             'host_utc':datetime.now(timezone.utc).isoformat(),'rtt_ms':rtts[i]*1000,
                             'health_host_age_s':stamp-health_times.get(i,stamp)}
                        row.update({k+'_deg':v for k,v in observation(d).items() if k != 'sat'})
                        row.update({'d_'+k:d[k] for k in DIAG_KEYS})
                        row.update({'h_'+k:v for k,v in health.get(i,{}).items() if k in HEALTH_KEYS})
                        host = host_snapshot() if host_snapshot else {}
                        row.update({'host_'+k:host[k] for k in HOST_KEYS if k in host})
                        writer.writerow(row)
                    except (OSError,RuntimeError,ValueError,TimeoutError) as e:
                        errors[i]=str(e)
                        metadata['errors'].append({'host_s':time.monotonic()-start,'axis':i,'error':str(e)})
                        # Diagnostic replies have no sequence ID. End the recorder
                        # on a failed transaction rather than reusing delayed replies.
                        raise
                now=time.monotonic()
                if now >= next_screen:
                    if runtime_log:
                        host = host_snapshot() if host_snapshot else {}
                        for i,d in data.items():
                            runtime_log.info('TELEMETRY %s', json.dumps({
                                'axis':i, 'host_s':now-start, 'diag':d, 'health':health.get(i,{}),
                                'health_host_age_s':now-health_times.get(i,now),
                                'rtt_ms':rtts[i]*1000, 'host':host,
                                'errors_deg':{k:v if math.isfinite(v) else None
                                              for k,v in observation(d).items() if k != 'sat'}}))
                    elif plain:
                        for i,d in data.items():
                            o=observation(d)
                            print(f"{now-start:7.2f}s axis={i} {name(STATES,d['state'])} fault={name(FAULTS,d['fault'])} "
                                  f"rx/traj/pos={human(d,'rx_mdeg'):.2f}/{human(d,'traj_mdeg'):.2f}/{human(d,'pos_mdeg'):.2f} "
                                  f"rpm={human(d,'v_mrpm'):.2f} planner/motor_err={o['planner_error']:.2f}/{o['motor_error']:.2f} "
                                  f"sat={d['sat']} rx_age={d['rx_age_ms']}ms {errors.get(i,'')}",flush=True)
                        if not data:print(errors,flush=True)
                    else:
                        ages={i:now-t for i,t in health_times.items()}
                        text=dashboard(data,health,windows,starts,errors,rtts,ages)
                        width,height=shutil.get_terminal_size((140,50))
                        lines=text.splitlines()
                        if not args.full and height<52:
                            keep=('GIMBAL','TILT /','PAN /','  State','  Position','  Error','  Speed','  Accel',
                                  '  Effort','  RX commands','  Health age','  FROZEN','  Waiting')
                            lines=[line for line in lines if line.startswith(keep)]
                            lines.append('Summary view; all data is in CSV. Enlarge terminal / --full for driver and timing details.')
                        wrapped=[]
                        for line in lines:
                            wrapped.extend(textwrap.wrap(line,width=max(30,width-1),subsequent_indent='    ',
                                                         replace_whitespace=False) or [''])
                        # Home + clear avoids cursor drift from terminal line wrapping.
                        sys.stdout.write('\x1b[H\x1b[2J'+'\n'.join(wrapped)+'\n')
                        sys.stdout.flush()
                    f.flush();next_screen=now+(1 if runtime_log or plain else .2)
                next_sample += 1/args.rate_hz
                if next_sample < now:next_sample=now+1/args.rate_hz
                if health_due:next_health=now+1/args.health_hz
    except KeyboardInterrupt:
        print('\nViewer stopped; motor command process was not changed.')
    except (OSError,RuntimeError,ValueError,TimeoutError) as e:
        exit_code = 1
        metadata['startup_error']=str(e)
        if runtime_log:
            runtime_log.warning('Telemetry unavailable/stopped: %s. Requires DIAG/HEALTH firmware; tracking continues.', e)
        else:
            print(f'FAIL: {e}. Rebuild/flash DIAG/HEALTH firmware if the board returns ERR syntax.')
    finally:
        for c in clients.values():c.close()
        metadata['elapsed_s']=time.monotonic()-start
        metadata['recent_metrics']={i:{k:v if math.isfinite(v) else None for k,v in metrics(windows[i]).items()}
                                    for i in axes}
        metadata_path.write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
        if runtime_log:
            runtime_log.info('Telemetry closed: counts=%s, metadata=%s', counts, metadata_path)
        else:
            print(f'Logs: {csv_path} / {metadata_path}')
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
