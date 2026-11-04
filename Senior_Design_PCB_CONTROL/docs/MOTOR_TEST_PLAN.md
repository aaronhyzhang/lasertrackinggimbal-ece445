# FIT1036 motor characterization at 24 V

2026-10-26. User reports approximately 150 RPM confirmed by encoder, with current
offset calibration completing and plausible displayed current.

Latest result, 2026-10-26: user reports the open-loop testing "all looks good".
The active BRINGUP configuration was inspected at that report and contains
**6.0 V phase peak, 350 RPM maximum command, and 150 RPM/s acceleration**, with
**axis 0 / J3** selected and the previously confirmed 24 V supply. Record this as
a user-reported successful bench operating point. Actual measured RPM at this
setting, trial duration, load, direction coverage, current and temperatures were
not supplied. Higher proposed matrix points (including 600/1100 RPM) are not
confirmed by this report. Maximum sustainable speed, acceleration and thermal
loading have **not** been established.

Use the existing `Senior_Design_PCB_BRINGUP` open-loop image for these trials.
The separate CONTROL project's first active mode is encoder-based sinusoidal
voltage commutation/position control; current-regulated FOC remains disabled.
Do not mix its low-speed commissioning gains with this speed-characterization test.

## What the motor documentation establishes

DFRobot identifies FIT1036 as 22 poles (11 pole pairs), rated 1100 RPM and 1.4 A
at 24 V, with a stated 4 A maximum whose duty and measurement definition are not
specified. Its operating-temperature listing is 0..60 C; it does not supply a
usable winding-temperature limit, inertia, acceleration limit or thermal model.
The published resistance and torque labels are inconsistent/ambiguous, so they
cannot establish a precise phase-current or torque limit for this board.
1100 RPM is a **rated speed**, not a guarantee at 2.5 V phase amplitude or with
your eventual gimbal load. [Manufacturer specifications](https://wiki.dfrobot.com/fit1036/)

## Conditions and stopping rules

Continuous speed sweeps apply only to a motor/fixture free to rotate continuously,
without cables or hard stops. Do not run them on the bounded one-turn mechanism.
On a constrained mechanism, characterize short moves after position control is
qualified; a continuous-spin RPM test is inappropriate there.

Use a current-limited 24 V supply. Start around 1 A supply limit, raising toward
2 A only if normal acceleration causes supply limiting and the phase-current and
temperature measurements support it. A bus-current limit does not limit winding
current, especially at low duty. Supply limiting can itself cause loss of sync;
log actual bus voltage and whether current limiting was active.

Measure motor case temperature independently (probe/IR with appropriate emissivity).
The board NTC is local inverter temperature. Until a thermal model is established,
use conservative **test aborts**: motor case 50 C, local board 60 C, sustained
sampled phase current above 1 A, bus droop/overshoot, driver fault, abnormal noise
or first skipping. These are provisional test rules, not manufacturer ratings or
implemented limits in the old open-loop firmware. Diagnostic current scans in that
image are not PWM-window-qualified RMS measurements; use an independent current
reference before accepting a higher-amplitude/continuous-duty result. If current
or motor temperature cannot be measured, stay at the previously demonstrated setting.

At skipping, STOP and reduce the demand; do not let it grind or remain stalled.
Record the first failure, then resume below it. Compare encoder RPM with requested
field RPM; persistent >10% error or a sudden irregular speed/phase relationship is
a provisional failed trial even if it recovers. Observe transient events with a
scope/log rather than relying on a 20 Hz Live Expressions snapshot.

## Test matrix: change one parameter at a time

Edit `Core/Inc/demo_config.h` in the working BRINGUP project and rebuild/flash.
Keep the repaired J3/J4 driver profiles identical, gain 20 V/V, hardware protection
enabled, and one motor selected. Confirm direction and neutral/S2 operation first.

| Trial | DEMO_OPEN_VOLTS (phase peak) | DEMO_OPEN_MAX_RPM | DEMO_OPEN_ACCEL_RPM_S | Expected ramp from rest |
| --- | --- | --- | --- | --- |
| Baseline | 2.5 V | 150 | 10 | 15 s |
| Voltage A | 3.0 V | 150 | 10 | 15 s |
| Voltage B | 3.5 V | 150 | 10 | 15 s |
| Voltage C | 4.0 V | 150 | 10 | 15 s |
| Voltage D, only after earlier passes | 5.0 V | 150 | 10 | 15 s |
| Initial voltage ceiling, after current/temperature qualification | 6.0 V | 150 | 10 | 15 s |
| Speed A | one qualified fixed voltage | 200 | 10 | 20 s |
| Speed B | same | 250 | 10 | 25 s |
| Speed C | same | 300 | 10 | 30 s |
| Speed D | same | 400 | 10 | 40 s |
| Speed E | same | 500 | 10 | 50 s |
| Initial speed ceiling | same | 600 | 10 | 60 s |
| Acceleration A..E | same | 300, or a lower qualified speed | 10, 20, 40, 80, 120 | 30, 15, 7.5, 3.75, 2.5 s to 300 |

The ramp times exclude driver wake/alignment and assume full pot command. Reach
each setting briefly first (5..10 s at speed), return to neutral, inspect current
and temperatures, then repeat in the other direction. High amplitude at low speed
can heat more than a faster unloaded run; passing a short voltage trial does not
justify continuous operation at that amplitude.

After 600 RPM passes at a qualified amplitude with current/thermal margin, a
separate extension is 700, 800, 900, 1000, 1100 RPM at low acceleration. Do not
increase voltage and RPM simultaneously. If no voltage <=6 V can sustain the
next speed, collect the current/voltage/temperature results before raising the
voltage ceiling. We cannot promise that 6 V will reach 1100 RPM.

After choosing a candidate continuous operating point, soak at least 20..30 min
and continue until temperature is approximately stable; 30 min alone may be
insufficient. Then repeat loaded acceleration, deceleration and reversals while
hot. Braking can regenerate into the 24 V bus; verify the supply can absorb it or
that suitable bus absorption/clamping is present before aggressive deceleration.

## Voltage and speed interpretation

The old sine modulator uses `duty=0.5+Vphase/24`, clamped to .05..95. Therefore
the nominal **unclipped modulation ceiling is 10.8 V phase peak at 24 V**. It is
not a permissible continuous motor-voltage recommendation. The old image assumes
24 V when calculating duty, so measure actual bus voltage.

For balanced sinusoidal commands, line-to-line RMS is approximately 1.225 times
phase peak: 2.5 V -> 3.06 V, 4 V -> 4.90 V, 6 V -> 7.35 V. This describes the
fundamental commanded voltage, not the 24 V PWM switch-node pulse amplitude.
More voltage can provide more torque/speed margin but also increases current and
heating; it cannot guarantee synchronization or a particular acceleration.

With 11 pole pairs, electrical frequency is RPM*11/60:
150 -> 27.5 Hz, 300 -> 55 Hz, 600 -> 110 Hz, 900 -> 165 Hz, 1100 -> 201.7 Hz.
Encoder filtering/transfer latency must be revisited before high-speed encoder
commutation; success in generated-angle open loop does not qualify the closed loop.

## Results to record

| Axis/connector | V phase peak | RPM commanded/measured | Accel RPM/s | Bus V / current-limit active? | Phase current/reference | Motor case start/end C | PCB C | Duration/load/direction | Pass or first skip |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| J3 / 0 | 2.5 | 150 / approximately 150 reported | 10 | 24 V nominal / unknown | calibration reported true; waveform qualification pending | not reported | not reported | unloaded, earlier observation | observed rotation; thermal pass pending |
| J3 / 0, active configuration inspected 2026-10-26 | 6.0 | 350 configured / measured value not supplied | 150 configured | 24 V nominal / not reported | not reported for this trial | not reported | not reported | duration/load/direction coverage not supplied | user reports testing all looks good; continuous thermal qualification pending |
| | | | | | | | | | |

Set operational limits only from repeatable hot/loaded results. A useful first
engineering margin is 20..25% below a demonstrated failure boundary, followed by
repeat validation; it is not a substitute for a thermal pass. Until those results
exist, retain the distinction between a successful bench setting and a qualified
operational maximum. The latest reported successful configuration is 6.0 V /
350 RPM / 150 RPM/s; the earlier baseline is 2.5 V / 150 RPM / 10 RPM/s. Neither
has been given a continuous thermal rating, and these open-loop settings do not
automatically change the new position controller's commissioning limits.
