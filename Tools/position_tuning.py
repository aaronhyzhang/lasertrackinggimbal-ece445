"""Timestamped sweep recording and conservative, single-change tuning advice.

All metrics are sampled laptop observations, not a hardware-qualified optimum.
Local configuration values are labels; they are NOT read back from firmware.
"""
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time


def read_config(path, axis=None):
    names = ('CONTROL_MAX_RPM', 'CONTROL_ACCEL_RPM_S', 'CONTROL_POSITION_KP',
             'CONTROL_VELOCITY_KP_V', 'CONTROL_VELOCITY_KI_V', 'CONTROL_RUN_VOLTS')
    path=Path(path)
    text = path.read_text()
    prefix='CONTROL_'
    profiles=path.parent/'motor_profiles.h'
    if '#include "motor_profiles.h"' in text and profiles.exists():
        if axis is None:
            selected=re.search(r'^\s*#define\s+PCB_TEST_AXIS\s+([01])\s*$',
                               (path.parent/'pcb_config.h').read_text(),re.M)
            if not selected:raise ValueError('Cannot determine selected motor profile; pass axis explicitly')
            axis=int(selected[1])
        if axis not in (0,1):raise ValueError('axis must be 0 (tilt/J3) or 1 (pan/J4)')
        prefix='TILT_' if axis==0 else 'PAN_'
        text=profiles.read_text()
    result = {}
    for name in names:
        label=prefix+name.removeprefix('CONTROL_')
        match = re.search(r'^\s*#define\s+' + label + r'\s+([0-9]+(?:\.[0-9]*)?)f?\s*$', text, re.M)
        if match:
            result[name] = float(match[1])
    return result


def metrics(move):
    samples = [s for s in move['samples'] if s['state'] == 7]
    result = dict(move=move['move'], target_deg=move['target_deg'], passed=move['passed'],
                  observed_faults=sorted({s.get('fault', 0) for s in move['samples'] if s.get('fault', 0)}))
    if not samples:
        return result
    start = samples[0]['position_deg']
    target = move['target_deg']
    distance = abs(target - start)
    direction = 1 if target >= start else -1
    t0 = samples[0]['time_s']
    first = next((s['time_s'] - t0 for s in samples if abs(s['position_deg'] - target) <= 2.5), None)
    overshoot = max(0, max(direction * (s['position_deg'] - target) for s in samples))
    duration = samples[-1]['time_s'] - t0
    gaps = [b['time_s'] - a['time_s'] for a, b in zip(samples, samples[1:])]
    acceleration = sorted(abs(b['rpm'] - a['rpm']) / dt
                          for a, b, dt in zip(samples, samples[1:], gaps) if dt > 0.02)
    result.update(start_deg=start, distance_deg=distance, direction=direction,
                  duration_s=duration, first_in_tolerance_s=first,
                  overshoot_deg=overshoot, final_error_deg=target - samples[-1]['position_deg'],
                  peak_rpm=max(abs(s['rpm']) for s in samples),
                  max_sample_gap_s=max(gaps, default=0), samples=len(samples))
    result['sampled_accel_p90_rpm_s'] = acceleration[int(0.9*(len(acceleration)-1))] if acceleration else None
    tail = [s for s in samples if s['time_s'] >= samples[-1]['time_s'] - 2]
    if len(tail) >= 8 and tail[-1]['time_s'] - tail[0]['time_s'] >= 1:
        result['tail_position_span_deg'] = max(s['position_deg'] for s in tail) - min(s['position_deg'] for s in tail)
        result['tail_peak_rpm'] = max(abs(s['rpm']) for s in tail)
        result['tail_mean_error_deg'] = target - sum(s['position_deg'] for s in tail)/len(tail)
    if first is not None:
        # Completion includes the mandatory 0.5-s measurement dwell. Separate
        # that deliberate delay from repeated departures/creep into tolerance.
        result['extra_settling_s'] = max(0, duration - first - 0.5)
        # Hysteresis avoids counting encoder jitter around the exact target.
        side = 0
        crossings = 0
        for sample in samples:
            if sample['time_s'] - t0 < first:
                continue
            error = sample['position_deg'] - target
            new_side = 1 if error > 2.5 else -1 if error < -2.5 else 0
            if new_side:
                if side and new_side != side:
                    crossings += 1
                side = new_side
        result['large_target_crossings'] = crossings
    return result


def recommend(results, config, error):
    def change(name, factor, explanation):
        if name not in config:
            return f'{explanation} Change {name} by {(factor-1)*100:+.0f}% after confirming the flashed settings.'
        old = config[name]
        return f'{explanation} Try only {name}: {old:g} -> {old*factor:.3g}; keep other settings unchanged.'
    settling_timeout = bool(error and 'TimeoutError' in error and 'Move/arm timed out' in error)
    if error and not settling_timeout:
        if 'fault=8' in error or "'fault': 8" in error:
            return change('CONTROL_ACCEL_RPM_S', 0.8,
                          'Following fault: the planned trajectory outpaced tracking.')
        return 'Run did not complete. Resolve the reported fault/communication/timeout before increasing speed or gains.'
    useful = [r for r in results if r.get('distance_deg', 0) >= 5 and r.get('samples', 0) >= 4]
    if not useful or (not settling_timeout and not all(r['passed'] for r in results)):
        return 'Insufficient completed motion data; repeat a logged tuning run before changing gains.'
    if any(r.get('observed_faults') for r in results):
        return 'A controller fault was observed. Resolve it before changing gains or increasing speed.'
    if max(r['max_sample_gap_s'] for r in useful) > 0.3:
        return 'Telemetry gaps exceed 300 ms; fix sampling/communication before drawing tuning conclusions.'
    if max(r.get('large_target_crossings', 0) for r in useful) >= 2:
        return ('Repeated large target crossings indicate oscillatory settling. Diagnostic next trial: '
                'set only CONTROL_VELOCITY_KI_V=0, keeping speed, acceleration and proportional gains unchanged. '
                'A remaining steady offset is useful evidence; do not raise speed yet.')
    stationary_offset = any(abs(r.get('tail_mean_error_deg', 0)) > 2.5 and
                            r.get('tail_position_span_deg', 999) <= 1 and
                            r.get('tail_peak_rpm', 999) <= 2 for r in useful)
    if stationary_offset and config.get('CONTROL_VELOCITY_KI_V') == 0:
        return change('CONTROL_VELOCITY_KP_V', 1.3,
                      'Integral-off run stopped outside tolerance with a stable offset. Trial more proportional effort; keep integral zero and watch for oscillation. Do not raise acceleration yet.')
    if max(r['overshoot_deg'] for r in useful) > 2.5:
        return change('CONTROL_VELOCITY_KI_V', 0.75,
                      'Observed overshoot exceeds 2.5 degrees; reduce integral buildup first. If hunting worsens, restore the prior value.')
    if max(r.get('extra_settling_s', 0) for r in useful) > 0.75:
        return change('CONTROL_POSITION_KP', 1.1,
                      'Long final correction with limited observed overshoot. A small position-gain increase is a trial, not a diagnosed cause; monitor overshoot.')
    if settling_timeout:
        return 'Movement timed out without a reported controller fault; inspect final offset and position/RPM traces before increasing speed.'
    ceiling = config.get('CONTROL_MAX_RPM')
    peak = max(r['peak_rpm'] for r in useful)
    if ceiling and peak >= 0.9 * ceiling:
        return change('CONTROL_MAX_RPM', 1.1,
                      'Completed moves approach the labelled speed ceiling with acceptable sampled settling.')
    return change('CONTROL_ACCEL_RPM_S', 1.1,
                  'Completed moves have acceptable sampled settling; test a small acceleration increase next.')


class Recorder:
    def __init__(self, directory, config, plan):
        self.started = time.monotonic()
        self.config = config
        self.plan = plan
        self.moves = []
        self.initial = None
        self.previous = None
        folder = Path(directory)
        folder.mkdir(parents=True, exist_ok=True)
        for previous in sorted(folder.glob('position_*.json'), reverse=True):
            try:
                report = json.loads(previous.read_text())
                if (report.get('plan_deg') == plan and not report.get('error')
                        and report.get('moves') and all(m['passed'] for m in report['moves'])):
                    self.previous = (previous.name, report)
                    break
            except (OSError, ValueError, KeyError):
                continue
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
        self.base = folder / f'position_{stamp}'
        self.file = self.base.with_suffix('.csv').open('w', newline='')
        self.writer = csv.DictWriter(self.file, fieldnames=(
            'time_s', 'move', 'target_deg', 'state', 'fault', 'position_deg', 'rpm',
            'current_age_us', 'encoder_age_us', 'firmware_settled'))
        self.writer.writeheader()

    def __call__(self, event, **data):
        if event == 'initial':
            self.initial = data['status']
        elif event == 'move':
            self.moves.append(dict(move=data['number'], target_deg=data['target']/1000,
                                   samples=[], passed=False))
        elif event == 'sample' and self.moves:
            s = data['status']
            row = dict(time_s=time.monotonic() - self.started, move=self.moves[-1]['move'],
                       target_deg=self.moves[-1]['target_deg'], state=s['state'], fault=s['fault'],
                       position_deg=s['position_mdeg']/1000, rpm=s['rpm_milli']/1000,
                       current_age_us=s.get('current_age_us', ''), encoder_age_us=s.get('encoder_age_us', ''),
                       firmware_settled=s.get('settled', ''))
            self.moves[-1]['samples'].append(row)
            self.writer.writerow(row)
            self.file.flush()
        elif event == 'complete':
            self.moves[-1]['passed'] = True

    def finish(self, error=None):
        self.file.close()
        results = [metrics(m) for m in self.moves]
        advice = recommend(results, self.config, error)
        report = dict(plan_deg=self.plan, local_config_unverified=self.config, initial_status=self.initial,
                      error=error, moves=results, recommendation=advice,
                      limitations='Laptop-timestamped ~12 Hz telemetry: peak speed/overshoot may be missed. '
                      'No trajectory or voltage readback; saturation and thermal limits cannot be inferred. '
                      'Confirm local config matches flashed firmware. Recommendations are manual trials, not automatic optimization.')
        if results and all(r['passed'] for r in results) and not error:
            total = sum(r.get('duration_s', 0) for r in results)
            report['total_measured_move_s'] = total
            if self.previous:
                previous_name, old = self.previous
                previous_total = old.get('total_measured_move_s')
                if previous_total and previous_total > 0:
                    report['comparison'] = dict(previous=previous_name,
                        previous_local_config_unverified=old.get('local_config_unverified'),
                        time_change_percent=(total/previous_total-1)*100)
        self.base.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
        print('\nMeasured tuning report (includes 0.5 s dwell per completed move):', flush=True)
        for r in results:
            if 'duration_s' in r:
                print(f"  Move {r['move']}: {r['distance_deg']:.1f} deg, {r['duration_s']:.2f} s, "
                      f"peak {r['peak_rpm']:.1f} RPM, overshoot {r['overshoot_deg']:.2f} deg, "
                      f"extra settling {r.get('extra_settling_s', 0):.2f} s, "
                      f"{'PASS' if r['passed'] else 'INCOMPLETE'}", flush=True)
        print('Next trial:', advice, flush=True)
        if 'comparison' in report:
            comparison = report['comparison']
            print(f"Time versus previous completed identical sequence: {comparison['time_change_percent']:+.1f}% "
                  f"({comparison['previous']}; negative is faster). Compare overshoot too.", flush=True)
        print('Config labels are from the local header; confirm they match the flashed image.', flush=True)
        print(f'Logs: {self.base.with_suffix(".csv")} and {self.base.with_suffix(".json")}', flush=True)


def reanalyze(path):
    path = Path(path)
    report = json.loads(path.read_text())
    moves = {m['move']: dict(move=m['move'], target_deg=m['target_deg'],
                            passed=m['passed'], samples=[]) for m in report['moves']}
    with path.with_suffix('.csv').open(newline='') as source:
        for row in csv.DictReader(source):
            move = moves[int(row['move'])]
            move['samples'].append(dict(time_s=float(row['time_s']), state=int(row['state']),
                                       fault=int(row['fault']), position_deg=float(row['position_deg']),
                                       rpm=float(row['rpm'])))
    report['moves'] = [metrics(m) for m in moves.values()]
    report['recommendation'] = recommend(report['moves'], report['local_config_unverified'], report['error'])
    output = path.with_name(path.stem + '_analysis.json')
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['recommendation'])
    print('Updated analysis:', output)
    return report


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Reanalyze a saved sweep JSON and its adjacent CSV without moving the motor.')
    parser.add_argument('report', type=Path)
    reanalyze(parser.parse_args().report)
