"""Loaded voltage-to-velocity identification and screened PI candidates.

Requires NumPy/SciPy. Models are local approximations, not thermal qualification.
"""
import csv
import json
import math
from pathlib import Path


def libraries():
    try:
        import numpy as np
        from scipy.optimize import least_squares
    except ImportError as error:
        raise RuntimeError('Install analysis dependencies: py -3 -m pip install -r Tools/requirements-tuning.txt') from error
    return np, least_squares


def simulate(parameters, t, u, initial=0):
    np, _ = libraries()
    a, b, friction, bias, delay, filtering = parameters
    delayed = np.interp(t-delay, t, u)
    out = np.empty(len(t));out[0] = initial
    speed = float(initial);filtered = float(initial)
    for i in range(1, len(t)):
        dt = float(t[i]-t[i-1])
        speed += dt*(a*float(delayed[i-1])-b*speed-friction*math.tanh(speed/0.05)+bias)
        filtered += (speed-filtered)*dt/(filtering+dt)
        out[i] = filtered
    return out


def identify(rows):
    np, least_squares = libraries()
    running = [r for r in rows if int(r['state']) == 7]
    if len(running) < 1000:
        raise RuntimeError('Inconclusive identification: need at least 1000 RUNNING samples')
    sequences = np.array([int(r['sequence']) for r in running])
    stamps = np.array([int(r['us']) for r in running], dtype=np.int64)
    # uint32 microsecond clock wrap is valid within a bounded trial.
    delta = (np.diff(stamps) & 0xffffffff)/1e6
    if np.any(np.diff(sequences) != 1) or np.any(delta <= 0) or np.max(delta) > 0.003:
        raise RuntimeError('Inconclusive identification: missing/reordered samples or >3 ms MCU gap')
    if any(int(r['fault']) for r in rows):
        raise RuntimeError('Inconclusive identification: controller fault in trace')
    if len({int(r['config_revision']) for r in running}) != 1:
        raise RuntimeError('Inconclusive identification: configuration changed during trace')
    if any(not (int(r['flags']) & 8) for r in running):
        raise RuntimeError('Inconclusive identification: encoder transport not valid')
    t = np.r_[0, np.cumsum(delta)]
    u = np.array([float(r['applied_v']) for r in running])
    y = np.array([float(r['velocity']) for r in running])
    if not np.all(np.isfinite(np.r_[u, y])) or np.std(u) < 0.03 or np.std(y) < 0.03:
        raise RuntimeError('Inconclusive identification: insufficient excitation/movement')
    if np.mean([bool(int(r['flags']) & 1) for r in running]) > 0.05:
        raise RuntimeError('Inconclusive identification: more than 5% saturated samples')
    # 500-Hz fitting retains the measured timestamps; original trace stays intact.
    t, u, y = t[::2], u[::2], y[::2]
    split = int(len(t)*0.65)
    dy = np.gradient(y[:split], t[:split])
    x = np.c_[u[:split], -y[:split], -np.tanh(y[:split]/0.05), np.ones(split)]
    linear = np.linalg.lstsq(x, dy, rcond=None)[0]
    start = [max(0.1, min(1000, linear[0])), max(0.01, min(100, linear[1])),
             max(0, min(30, linear[2])), max(-30, min(30, linear[3])), 0.002, 0.006]
    lower = [0.001, 0, 0, -500, 0, 0]
    upper = [10000, 200, 500, 500, 0.03, 0.05]
    scale = max(float(np.std(y[:split])), 0.05)
    fit = least_squares(lambda p:(simulate(p,t[:split],u[:split],y[0])-y[:split])/scale,
                        start, bounds=(lower,upper), loss='soft_l1', max_nfev=500,
                        x_scale='jac')
    prediction = simulate(fit.x, t[split:]-t[split], u[split:], y[split])
    rmse = float(np.sqrt(np.mean((prediction-y[split:])**2)))
    norm = max(float(np.std(y[split:])),0.05)
    relative = rmse/norm
    condition = float(np.linalg.cond(fit.jac))
    variance=float(np.sum(fit.fun**2)/max(1,len(fit.fun)-len(fit.x)))
    standard=np.sqrt(np.maximum(0,np.diag(np.linalg.pinv(fit.jac.T@fit.jac))*variance))
    accepted = bool(fit.success and relative <= 0.3 and condition < 1e8)
    report = dict(schema=1, model='omega_dot=a*V-b*omega-c*tanh(omega/0.05)+bias; sensor LPF+delay',
                  parameters=dict(zip(('a','b','friction','bias','delay_s','filter_s'),map(float,fit.x))),
                  accepted=accepted, holdout_rmse_rad_s=rmse, holdout_normalized_rmse=relative,
                  jacobian_condition=condition, samples=len(running), fit_fraction=0.65,
                  optimizer=dict(converged=bool(fit.success),status=int(fit.status),
                                 message=str(fit.message),evaluations=int(fit.nfev)),
                  approximate_local_parameter_std=list(map(float,standard)),
                  warning='Local voltage model. Physical J/Kt/R are not separately identified. Validate other directions/poses and load.')
    if accepted:
        report['candidates'] = candidates(report['parameters'])
    else:
        report['candidates'] = []
        failures=[]
        if not fit.success:failures.append(f'Optimizer did not converge: {fit.message}')
        if relative>0.3:failures.append(f'Holdout normalized RMSE {relative:.3f} exceeds 0.300')
        if condition>=1e8:failures.append(f'Jacobian condition {condition:.3g} exceeds limit')
        report['reason'] = '; '.join(failures)+'; no gains automatically applied.'
    return report


def candidates(p):
    np, _ = libraries()
    result = []
    # A highly damped plant requires wn > b/(2*zeta) for positive Kp.
    # The earlier grid ending at 24 rad/s silently excluded such plants.
    # Higher candidates still require the same measured-delay phase margin
    # and saturated discrete simulation checks; range alone is not approval.
    for wn in (2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256):
        for damping in (0.8, 1.0, 1.2):
            # Screen the millivolt-resolution gains the board will actually use.
            kp = round((2*damping*wn-p['b'])/p['a'],3)
            ki = round(wn*wn/p['a'],3)
            if not (0 < kp <= 50 and 0 < ki <= 500):
                continue
            w = np.logspace(-2, 4, 6000)
            loop = (kp+ki/(1j*w))*p['a']/(1j*w+p['b'])/(1+1j*w*p['filter_s'])*np.exp(-1j*w*(p['delay_s']+0.0005))
            cross = np.flatnonzero((np.abs(loop[:-1])>=1)&(np.abs(loop[1:])<1))
            if len(cross) != 1:
                continue
            phase = np.unwrap(np.angle(loop))
            margin = 180+float(phase[int(cross[0])]*180/np.pi)
            if margin < 50:
                continue
            # Nonlinear saturated discrete velocity-loop screening, both signs.
            # This is a local simulated check, never a replacement for holdout
            # measurements on the mechanism.
            worst_error=0.0
            for direction in (-1,1):
                speed=filtered=integral=0.0;errors=[];history=[]
                dt=0.001
                for j in range(3000):
                    reference=direction*0.5
                    error=reference-filtered
                    candidate_i=integral+ki*error*dt
                    request=kp*error+candidate_i
                    applied=max(-6,min(6,request))
                    if abs(request)<=6 or error*request<0:integral=candidate_i
                    history.append(applied)
                    delayed=history[max(0,j-round(p['delay_s']/dt))]
                    speed+=dt*(p['a']*delayed-p['b']*speed-p['friction']*math.tanh(speed/0.05)+p['bias'])
                    filtered+=(speed-filtered)*dt/(p['filter_s']+dt)
                    if j>2500:errors.append(abs(reference-filtered))
                worst_error=max(worst_error,sum(errors)/len(errors))
            if worst_error>0.15:continue
            result.append(dict(velocity_kp=round(kp,3),velocity_ki=round(ki,3),
                               position_kp=round(wn/5,3),natural_frequency_rad_s=wn,
                               damping=damping,predicted_phase_margin_deg=margin,
                               simulated_tail_velocity_error_rad_s=worst_error))
    # Start at lower bandwidth, then increase. Prefer critical damping at each
    # bandwidth; repeated variants of the fastest candidate are not a useful
    # first hardware experiment.
    selected=[]
    for wn in sorted({c['natural_frequency_rad_s'] for c in result}):
        group=[c for c in result if c['natural_frequency_rad_s']==wn]
        selected.append(min(group,key=lambda c:abs(c['damping']-1)))
    return selected


def score(rows, tolerance=1.0):
    records = [r for r in rows if int(r['state']) == 7]
    if not records:
        return dict(passed=False,reason='No RUNNING records')
    segments = []
    start = 0
    for i in range(1,len(records)+1):
        if i<len(records) and abs(float(records[i]['target'])-float(records[start]['target']))<0.001:
            continue
        chunk = records[start:i];start=i
        target = float(chunk[0]['target'])
        t0 = int(chunk[0]['us']);stable=None;settled=None
        times=[((int(r['us'])-t0)&0xffffffff)/1e6 for r in chunk]
        positions=[float(r['position']) for r in chunk]
        last_bad=max((j for j,v in enumerate(positions) if not math.isfinite(v) or abs(v-target)>tolerance),default=-1)
        arrival=times[last_bad+1] if last_bad+1<len(chunk) else None
        # Find the final stable interval once. The old half-second reset loop
        # could add an unnecessary second dwell or accept a temporary hold
        # followed by losing the target later in the same segment.
        low=math.inf;high=-math.inf
        for j in range(len(chunk)-1,-1,-1):
            value=positions[j];low=min(low,value);high=max(high,value)
            if not math.isfinite(value) or abs(value-target)>tolerance or high-low>0.5:break
            stable=j
        if stable is not None:
            settled=next((t for t in times if t-times[stable]>=0.5),None)
        distance=target-float(chunk[0]['position']);direction=1 if distance>=0 else -1
        segments.append(dict(target=target,distance_deg=abs(distance),arrival_s=arrival,
                             stable_start_s=times[stable] if stable is not None else None,settling_s=settled,
                             overshoot_deg=max(0,max(direction*(float(r['position'])-target) for r in chunk)),
                             final_error_deg=target-float(chunk[-1]['position'])))
    useful=[s for s in segments if s['distance_deg']>=2]
    passed=bool(useful and all(s['settling_s'] is not None for s in useful) and not any(int(r['fault']) for r in rows))
    return dict(score_schema=2,passed=passed,segments=segments,
                worst_arrival_s=max((s['arrival_s'] if s['arrival_s'] is not None else 1e6 for s in useful),default=1e6),
                worst_settling_s=max((s['settling_s'] or 1e6 for s in useful),default=1e6),
                worst_overshoot_deg=max((s['overshoot_deg'] for s in useful),default=0))


def performance_key(score):
    """Rank qualified position trials by the user's +/-1-degree arrival goal.

    Confirmation/overshoot remain qualification checks, not a competing
    optimization objective. Schema-1 reports lack arrival and retain their
    old timing key; rescore their CSVs for comparison with current results.
    """
    return (score.get('worst_arrival_s',score['worst_settling_s']),score['worst_settling_s'])


def score_velocity(rows):
    np,_=libraries()
    data=[r for r in rows if int(r['state'])==7]
    if len(data)<100:return dict(passed=False,rmse_rad_s=1e6,reason='Insufficient velocity records')
    target=np.array([float(r['velocity_target']) for r in data])
    actual=np.array([float(r['velocity']) for r in data])
    rmse=float(np.sqrt(np.mean((actual-target)**2)))
    magnitude=max(float(np.max(np.abs(target))),0.01)
    return dict(passed=bool(not any(int(r['fault']) for r in rows) and rmse/magnitude<0.5),
                rmse_rad_s=rmse,normalized_rmse=rmse/magnitude,
                peak_rad_s=float(np.max(np.abs(actual))))


def svg(rows,path):
    if not rows:return
    try:
        import matplotlib
        matplotlib.use('Agg')
        from matplotlib import pyplot as plt
    except ImportError as error:
        raise RuntimeError('Install plotting dependencies: py -3 -m pip install -r Tools/requirements-tuning.txt') from error
    first=int(rows[0]['us']);t=[((int(r['us'])-first)&0xffffffff)/1e6 for r in rows]
    channels=(('position','target','Position (deg)'),('applied_v','requested_v','Voltage (V)'),('velocity','velocity_target','Velocity (rad/s)'))
    fig,axes=plt.subplots(3,1,figsize=(11,8),sharex=True,layout='constrained')
    for axis,(one,two,label) in zip(axes,channels):
        axis.plot(t,[float(r[one]) for r in rows],label=one,linewidth=1)
        axis.plot(t,[float(r[two]) for r in rows],label=two,linewidth=1,alpha=0.8)
        axis.set_ylabel(label);axis.grid(alpha=0.25);axis.legend(loc='best')
    axes[-1].set_xlabel('MCU elapsed time (s)')
    fig.suptitle('Commissioning trace: measured feedback and applied command')
    fig.savefig(path);fig.savefig(Path(path).with_suffix('.png'),dpi=130);plt.close(fig)


def load(path):
    with Path(path).open(newline='') as source:return list(csv.DictReader(source))
