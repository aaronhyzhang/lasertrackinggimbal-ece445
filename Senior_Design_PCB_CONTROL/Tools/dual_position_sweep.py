"""Concurrent J3 tilt / J4 pan sweeps. Physical S1 arms both once; S2 stops both."""
import argparse
import csv
import json
import math
from pathlib import Path
import random
import time
from datetime import datetime, timezone
from position_client import Client


def plans(low, high, count, seed):
    rng=random.Random(seed);mid=(low+high)/2
    return [low,high,low,mid,mid+5,mid-5,mid+25,mid-25,
            *[rng.uniform(low,high) for _ in range(count)],low,high,low]


def run(clients, targets, arm_timeout=60, move_timeout=30, emit=lambda *a:None):
    initial=[c.request('STATUS') for c in clients]
    for axis,s in enumerate(initial):
        if not (s['axis']==axis and str(s.get('dual'))=='1' and s['source']==1 and s['enabled']
                and s['calibrated'] and s['zero'] and s['state']==5 and not s['fault']):
            raise RuntimeError(f'Axis {axis} requires dual firmware, READY, calibration and zero: {s}')
    results=[]
    running=[False,False]
    attempted=False
    try:
        for move,pair in enumerate(zip(*targets),1):
            desired=[round(v*1000) for v in pair]
            attempted=True
            for axis,c in enumerate(clients):c.request('POSITION',desired[axis])
            print(f'Move {move}: tilt={pair[0]:.2f}, pan={pair[1]:.2f} deg',flush=True)
            if not all(running):print('Press S1 once to start both motors. S2/Ctrl-C stops both.',flush=True)
            deadline=time.monotonic()+(move_timeout if all(running) else arm_timeout)
            move_start=time.monotonic() if all(running) else None
            stable=[None,None];lo=[0,0];hi=[0,0];reported=0
            while True:
                samples=[]
                for axis,c in enumerate(clients):
                    try:c.request('HEARTBEAT')
                    except RuntimeError as exc:
                        raise RuntimeError(f'Axis {axis} heartbeat rejected: {c.request("STATUS")}') from exc
                    s=c.request('STATUS');samples.append(s)
                    emit(move,axis,desired[axis],s)
                    if s['axis']!=axis or s['fault']:raise RuntimeError(f'Axis {axis} fault/identity: {s}')
                    if s['state']==7:running[axis]=True
                    elif running[axis] or s['state'] not in (5,6):
                        raise RuntimeError(f'Axis {axis} stopped; no automatic rearm: {s}')
                now=time.monotonic()
                if move_start is None and all(running):move_start=now
                if all(running) and any(s['state']==6 for s in samples):raise RuntimeError('Axis returned to waking')
                for axis,s in enumerate(samples):
                    pos=s['position_mdeg']
                    eligible=(s['state']==7 and s['target_mdeg']==desired[axis] and s['settled']
                              and abs(pos-desired[axis])<=1000)
                    if not eligible:stable[axis]=None
                    else:
                        if stable[axis] is None:stable[axis]=now;lo[axis]=hi[axis]=pos
                        lo[axis]=min(lo[axis],pos);hi[axis]=max(hi[axis],pos)
                        if hi[axis]-lo[axis]>500:stable[axis]=now;lo[axis]=hi[axis]=pos
                if now>=reported:
                    print('  '+', '.join(f'{"tilt" if i==0 else "pan"}: {s["position_mdeg"]/1000:.2f} deg state={s["state"]}' for i,s in enumerate(samples)),flush=True)
                    reported=now+.5
                if all(v is not None and now-v>=.5 for v in stable):
                    elapsed=now-move_start
                    results.append(dict(move=move,targets_deg=list(pair),completion_s=elapsed,final_error_deg=[(s['position_mdeg']-desired[i])/1000 for i,s in enumerate(samples)]))
                    print(f'PASS pair {move}: both within +/-1 degree and stable in {elapsed:.3f} s (includes dwell).',flush=True);break
                if now>=deadline:raise TimeoutError(f'Move/arm timed out at pair {move}')
                time.sleep(.06)
    finally:
        if attempted:
            for c in clients:
                try:c.request('DISABLE')
                except (OSError,RuntimeError,TimeoutError,ValueError):pass

    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',default='192.168.10.2');p.add_argument('--tilt-port',type=int,default=5005)
    p.add_argument('--pan-port',type=int,default=5006)
    p.add_argument('--tilt-min',type=float,default=15);p.add_argument('--tilt-max',type=float,default=345)
    p.add_argument('--pan-min',type=float,default=15);p.add_argument('--pan-max',type=float,default=320)
    p.add_argument('--smoke',action='store_true',help='Four small paired targets near each minimum')
    p.add_argument('--random-moves',type=int,default=6);p.add_argument('--seed',type=int,default=1)
    p.add_argument('--arm-timeout',type=float,default=60);p.add_argument('--move-timeout',type=float,default=30)
    p.add_argument('--log-dir',type=Path,default=Path('logs/dual_sweep'));p.add_argument('--dry-run',action='store_true')
    a=p.parse_args()
    for low,high,maximum in ((a.tilt_min,a.tilt_max,358),(a.pan_min,a.pan_max,337)):
        if not all(math.isfinite(v) for v in (low,high)) or not 2<=low<high<=maximum or high-low<50:
            p.error('Invalid axis window; requires >=50 degree span inside firmware bounds')
    if not 0<=a.random_moves<=100 or not all(math.isfinite(v) and v>0 for v in (a.arm_timeout,a.move_timeout)):
        p.error('Invalid count/timeout')
    if a.tilt_port==a.pan_port or not all(1<=v<=65535 for v in (a.tilt_port,a.pan_port)):p.error('Use distinct valid ports')
    targets=[plans(a.tilt_min,a.tilt_max,a.random_moves,a.seed),plans(a.pan_min,a.pan_max,a.random_moves,a.seed+1)]
    if a.smoke:targets=[[low,low+10,low+5,low] for low in (a.tilt_min,a.pan_min)]
    if a.dry_run:print(json.dumps(dict(tilt=targets[0],pan=targets[1]),indent=2));return
    a.log_dir.mkdir(parents=True,exist_ok=True)
    base=a.log_dir/('dual_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    clients=[];summary=dict(plan=targets,ports=[a.tilt_port,a.pan_port],completed=False,error=None)
    try:
        clients=[Client(a.host,a.tilt_port),Client(a.host,a.pan_port)]
        summary['config']=[c.request('CONFIG') for c in clients]
        with base.with_suffix('.csv').open('w',newline='') as f:
            w=csv.writer(f);w.writerow(['time_s','move','axis','target_deg','position_deg','rpm','state','fault','settled'])
            start=time.monotonic()
            def emit(move,axis,target,s):
                w.writerow([time.monotonic()-start,move,axis,target/1000,s['position_mdeg']/1000,s['rpm_milli']/1000,s['state'],s['fault'],s['settled']]);f.flush()
            summary['moves']=run(clients,targets,a.arm_timeout,a.move_timeout,emit)
        summary['completed']=True
    except BaseException as e:
        summary['error']=f'{type(e).__name__}: {e}';raise
    finally:
        for c in clients:c.close()
        base.with_suffix('.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(f'Logs: {base}.csv / .json',flush=True)

if __name__=='__main__':
    try:main()
    except KeyboardInterrupt:print('Stopped.');raise SystemExit(130)
    except (OSError,RuntimeError,TimeoutError,ValueError) as e:print(f'FAIL: {e}');raise SystemExit(1)
