# Local position tuning checkpoint, 2026-10-27

Axis 0 / J3; sinusoidal encoder voltage commutation, current PI off; 24-V supply.
User reports the representative mechanism/load. This is a local checkpoint near
180 degrees, not a full-travel or continuous thermal maximum.

## Qualified local gain pair

| Setting | Value |
|---|---:|
| Velocity Kp (V per rad/s) | 0.215 |
| Velocity Ki (V per rad) | 18.795 |
| Position Kp (1/s) | 5 |
| Speed limit (RPM) | 100 |
| Acceleration (RPM/s) | 75 |
| Run voltage / voltage ceiling (phase peak V) | 6 / 6 |

Source: `logs/position_candidate3/verify_report.json`, recommended_config revision
23. A baseline with velocity Kp/Ki 0.5/0.5 repeatedly stopped approximately
1.75 degrees short of the 185-degree target during the three-second segment.
The new gain pair passed the 175/185-degree trial and both repeated
176.5/183.5-degree holdout trials. Position Kp and trajectory limits did not change.
The script restored the original gains after testing; applying the pair through
SET is a separate step. SET values are RAM-only; compiled defaults remain 0.5/0.5.

## Measured response

- Approximately 10-degree moves: enters and remains within +/-1 degree in
  0.265..0.273 seconds; final stable-interval confirmation in 0.829..0.936 seconds.
- Holdouts (approximately 7 degrees): enters and remains within +/-1 degree in
  0.199..0.222 seconds; confirmation in 0.797..0.901 seconds.
- Largest overshoot in these position/holdout records: approximately 0.104 degree.
- Last-half-second encoder error: at most approximately 0.104 degree; span one
  AS5600 count (0.087890625 degree). This is encoder-reported repeatability, not
  independent absolute-angle metrology; the weak-field warning remains present.
- Largest sampled phase current: approximately 0.107 A; largest commanded phase
  amplitude: approximately 0.685 V; largest reported speed: approximately 14.4 RPM.
- No recorded faults, lost trace sequences or voltage saturation. Board NTC
  peaked near 29.1 C; it is not a motor-case temperature measurement.

The original schema-1 report listed approximately 1.27-second confirmation for
the 10-degree moves. Schema 2 separates arrival from final stability confirmation
and removes a conservative repeated-dwell artifact. Preserve original reports;
rescore CSVs when comparing across score versions.

## Completed acceleration search

Source: `logs/acceleration_first/envelope_report.json`. All steps passed their
repeated +/-5-degree tests around 180 degrees. Current gains stayed 0.215/18.795/5,
speed cap 100 RPM and phase-voltage ceiling 6 V.

| Acceleration (RPM/s) | Worst +/-1-degree arrival (ms) | Worst confirmation (ms) |
|---:|---:|---:|
| 75 (baseline) | 275 | 905 |
| 86.25 | 262 | 957 |
| 99.187 | 244 | 930 |
| 114.066 | 240 | 937 |
| 131.175 | 238 | 908 |
| 150 | 200 | 906 |

150 RPM/s is the largest passing local acceleration tested so far. Worst arrival
improved 27% over 75 RPM/s. At 150 RPM/s, maximum sampled phase current was
approximately 0.114 A, applied amplitude 0.758 V and reported peak speed 20.1 RPM.
All captures were fault-free, with no trace sequence gaps or voltage saturation.
Overshoot remained below 0.10 degree. Board NTC peaked near 30 C.

The original report selected 75 RPM/s because its worst confirmation time was
0.905038 s versus 0.905964 s at 150 RPM/s (0.926-ms difference). That result did
not reflect the user's objective: reach and remain within +/-1 degree as quickly
as possible, with stability a required pass. The updated tool ranks worst arrival
among qualifying trials, using confirmation time only to break ties. The same
saved records select 150 RPM/s; the sweep need not be repeated. Original reports
remain intact. The script restored 75 RPM/s and retained the selected gains.

## Next experiment

Set the passing 150-RPM/s baseline while inhibited (gains remain applied):

```powershell
py -3 .\Tools\run_commission.py set --accel-rpm-s 150
```

Then extend the acceleration search while preserving the tested local target range:

```powershell
py -3 .\Tools\run_commission.py envelope --axis 0 --search acceleration --ceiling 300 --centre 180 --radius 30 --test-degrees 5 --max-trials 5 --out logs\acceleration_second
```

One S1 starts the batch. The script records a baseline then tests about
172.5, 198.375, 228.13, 262.35 and 300 RPM/s, twice each. This is an experimental
ceiling, not a rated maximum. It recommends the fastest qualifying worst arrival,
separately records the highest passing limit, and restores the initial 150-RPM/s
setting afterward. The selected velocity gains remain applied.
The local moves do not approach the 100-RPM cap; raising that cap alone has no
expected benefit here. Larger moves, both directions/poses and motor temperature
require later qualification.
