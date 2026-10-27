# Measured commissioning tool

Built 2026-10-27. Software-tested, not yet qualified on this assembled mechanism.
This tool keeps encoder-based sinusoidal voltage control; current PI stays off.

## The 6-V correction

The old controller clipped at bus_v*0.15 because its PWM range was 35..65%.
Voltage mode now uses 25..75%, so its bus-dependent ceiling is bus_v*0.25:

    effective_limit = min(runtime run_volts, CONTROL_VOLT_LIMIT, bus_v*0.25)

At 24 V with both configured limits 6 V, 6 V phase peak is available. At 18 V
the ceiling is 4.5 V. This is phase command amplitude, not motor line-to-line
RMS or DC supply voltage. position_voltage_limit() is shared by the velocity
integrator and PWM stage, so anti-windup recognizes the actual available drive.
The prepared current-PI mode retains its earlier 35..65% duty range.

The injected ADC trigger is unchanged: CNT2300 rising, approximately 2 us before
the PWM peak, with approximately 6-us conversion sequence. At maximum 75% duty,
the ideal common low-side window is 12.5 us wide, leaving the designed sequence
inside it. Actual gate/CSA settling, latency, amperes and current protection must
be checked on the board before treating 6 V as qualified. No current limit or
driver/STOP/encoder/travel protection was disabled by this change.

## Ready now without the PCB

From the active CONTROL project folder:

```powershell
py -3 .\Tools\run_commission.py demo --out logs\commission_demo
```

The launcher uses the workspace-local Python environment already installed on
this laptop. On another machine it creates build/commission-venv and installs
Tools/requirements-tuning.txt (NumPy, SciPy, Matplotlib). No global Python packages
are required. The demo never creates a socket. It generates a known synthetic
voltage/velocity plant, identifies it using only the first 65% of the record,
validates against the remaining 35%, computes PI candidates and exports CSV,
JSON, SVG and PNG. Synthetic results are not motor gains to copy into firmware.

## First hardware session

1. Flash the new CONTROL Debug image. Existing calibration/zero/sweep scripts
   remain usable. The build keeps the user's selected axis, Ethernet mode,
   voltage mode and gain defaults. RAM parameters reset on power/reset.
2. Verify configuration without motion:

```powershell
py -3 .\Tools\run_commission.py info
```

Expect CONFIG schema=1, build label commission-20261027, selected axis/source,
active parameters, revision/checksum, duty_span=250 and voltage_limit=6000
(wire units are thousandths; human units are printed too). Readback is from
firmware, not a local header. STATUS and TESTINFO are read-only.

3. Use calibrate_motor.py and Enter to calibrate/zero. With outputs inhibited,
   position the mechanism near the chosen test centre (default 180 degrees).
   Preserve zero while positioning. The existing position_client can do a move
   to centre and disable; manual movement in READY also preserves the coordinate
   provided encoder continuity remains valid. Verify its coordinate via STATUS.
4. Begin with a short bounded capture and inspect its trace:

```powershell
py -3 .\Tools\run_commission.py capture --axis 0 --mode prbs --amplitude 0.3 --period-ms 100 --duration 4 --centre 180 --radius 30 --out logs\first_capture
```

The voltage excitation starts only after physical S1. The amplitude is volts
for voltage/prbs modes, RPM for velocity, degrees for position. The selected
centre/radius is an experiment-only operating window, not a new normal-motion
boundary. The MCU aborts if it leaves that window. The 0.3-V starting amplitude
may be insufficient to overcome this load's friction; an inconclusive trace
means measure/adjust excitation, not apply guessed gains.

Position tests use a 6-second square-reference period (3 s at each target) and
at least 12 seconds total. Voltage pulses use --period-ms, half-positive then
half-negative. PRBS uses --period-ms chip duration and an opposite-sign partner
for each chip, avoiding nonzero mean excitation. Velocity validation uses
small +/-3 RPM references with an 800-ms period and 6.4-second trials (eight
complete cycles, with equal positive/negative reference duration).

S2/Ctrl-C/disconnect stops. A fault or operator stop revokes batch authorization;
the tool never clears a fault or automatically rearms after that event. One S1
press permits subsequent normally completed experiments in the same live batch.
The existing 500-ms lease remains active. Continuous heartbeat runs on a
serialized background connection while analysis/plotting occurs.

## Analyze and derive candidate gains

```powershell
py -3 .\Tools\run_commission.py analyze --trace logs\first_capture\<timestamp>_trial.csv --out logs\first_model
```

Identification requires >=1000 consecutive RUNNING records with one configuration,
valid encoder transport, no controller faults, <=3-ms MCU sample gaps, enough
input/output variation and <=5% saturated samples. Missing/overwritten records
invalidate identification. Incomplete/faulted metadata is rejected by analyze.
These are model-quality checks, not additional normal motor fault gates.

The model is:

    omega_dot = a*V_applied - b*omega - friction*tanh(omega/0.05) + bias

with estimated input delay and sensor first-order filtering. Fit uses measured
MCU time and actual applied voltage; filtered-speed simulation is fitted without
raw angle differentiation. Robust bounded output-error fitting uses 65% of
data; holdout normalized RMSE must be <=0.3, fit must converge and local Jacobian
conditioning must be adequate. Approximate local parameter standard deviations
are recorded; correlated residuals/nonlinearities can make them optimistic.

For the linear part a/(s+b), Kp=(2*zeta*wn-b)/a and Ki=wn^2/a provide candidates.
Candidates include measured filtering/delay, must predict >=50-degree phase
margin, and undergo nonlinear saturated discrete velocity simulation in both
directions. Lower-bandwidth candidates are tried before higher ones. This is
a local effective voltage model, not a separate physical J/Kt/R measurement.
An inadequate model yields no automatically applied candidate. The optimizer
allows up to 500 evaluations and reports convergence/status/evaluation count.
Candidate natural frequencies span 2..256 rad/s; the measured-delay phase-margin
and nonlinear simulation checks still decide which are usable. A plant with
large fitted damping b needs wn > b/(2*zeta) for positive proportional gain;
the earlier grid ending at 24 rad/s could exclude every candidate.

## Automatic local loop tuning

Only after the first capture and polarity/window checks:

```powershell
py -3 .\Tools\run_commission.py auto --axis 0 --amplitude 0.3 --period-ms 100 --duration 8 --centre 180 --radius 30 --test-degrees 10 --max-trials 4 --out logs\auto_session
```

To reuse a completed voltage/prbs capture, add --trace instead of repeating
open-voltage excitation. For example, the first short hardware capture:

```powershell
py -3 .\Tools\run_commission.py auto --trace logs\capture_06v_short\20261027T155629_248934Z_trial.csv --axis 0 --centre 180 --radius 30 --test-degrees 5 --max-trials 1 --out logs\auto_first
```

The CSV needs its adjacent completed, error-free JSON. Its firmware build,
axis/source, gains, speed/acceleration and voltage limits must match live
configuration readback. The recorded trace is fitted offline first; physical S1
then starts the baseline and the rest of the controlled trial batch. There is
no need to reflash, recalibrate or change mechanical zero for this Python update.
The short single-frequency capture is a local starting model; the subsequent
velocity and position holdouts, not its fit alone, decide recommendations.

The tool identifies the voltage model, records a baseline position trial, tests
velocity PI candidates with the position correction isolated, then freezes
the selected inner gains and validates position gains around the model-derived
starting point. It keeps a passing baseline over a slower candidate. Two
different-amplitude holdout trials check the proposed winner. The normal
position/velocity controller, limits and 1-kHz supervisor remain the execution
path; arbitrary Ethernet packets never write PWM duties directly.

Scoring requires entering +/-1 degree for 0.5 s with <=0.5-degree position span.
Score schema 2 reports arrival_s (enters +/-1 degree and stays), stable_start_s
(start of the final <=0.5-degree span interval), and settling_s (that start plus
the 0.5-second confirmation dwell). A temporary hold followed by losing the
target cannot pass. Earlier schema-1 reports could overcount confirmation time;
compare timings using the same schema or rescore their original CSVs.
Maximum scored overshoot defaults to 2 degrees (--max-overshoot). Candidate
selection minimizes the worst time to enter and remain within +/-1 degree,
with stability/overshoot required to pass. Confirmation time breaks arrival
ties. It does not optimize the extra confirmation dwell. Configuration
changes happen only while inhibited in READY/OFF and clear controller integrals.
Trial timing comes from the MCU; Windows does not time the excitation edges.

Files include verified parameter readback, trace timestamps/sequence/config ID,
position, target, trajectory, velocity target/error, P and I terms, requested
and applied voltage, saturation/validity flags, bus/current/temperature/state
and first relevant fault evidence. CSV and JSON survive partial failures;
PNG/SVG plots compare position/target, applied/requested voltage and velocity.
auto_report.json contains models, rejected trials, scores and recommended_config.

On normal exit the RAM baseline is restored. On a latched fault SET may be
rejected; DISABLE is still sent and resetting restores compiled defaults.
The winner is a recommendation, not automatically saved to flash/source.
Readback wire fields in recommended_config are thousandths. Apply explicitly:

```powershell
py -3 .\Tools\run_commission.py set --velocity-kp <value> --velocity-ki <value> --position-kp <value>
```

The SET command prints verified readback and releases its network ownership
while keeping the gains, so the next script can take ownership immediately.
Values are RAM-only; reset restores the compiled defaults. The command above
contains placeholders, not runnable gain suggestions. Runtime scalar order on
wire is 0 velocity Kp,
1 velocity Ki, 2 position Kp, 3 max RPM, 4 acceleration RPM/s, 5 run volts.

## Explicit position validation of a tested gain pair

The preliminary velocity-step RMS screen is not the final position objective.
It includes reversal delay and encoder speed quantization; passing a fitted
model alone also does not establish hardware performance. If a hardware-tested
gain pair improves tracking but fails that screen, verify it explicitly against
the position requirement without relabeling its velocity score:

```powershell
py -3 .\Tools\run_commission.py verify --velocity-kp 0.215 --velocity-ki 18.795 --axis 0 --centre 180 --radius 30 --test-degrees 5 --out logs\position_candidate3
```

These are the third candidate from the first measured model, not universal gains.
The command first records a new baseline position trial, changes only the two
specified inner-loop gains, and records the same +/-5-degree position trial.
Position Kp, speed, acceleration and voltage limits remain at their baseline.
If the candidate meets the existing +/-1-degree stable position/overshoot score,
two +/-3.5-degree holdouts follow. A recommendation requires those holdouts and
an improvement over the baseline. Failed position trials get no recommendation.
The preliminary velocity result is unchanged. One S1 starts the batch; S2 or
Ctrl-C stops it. `verify_report.json` records scores, gain readback and whether
baseline restoration was acknowledged. The command attempts to restore the
baseline in every exit path; faults may require reset as in auto. No reflash is
needed for this Python-only addition.

## Later acceleration and speed search

After repeated position tuning passes with the representative load, explicitly
apply the accepted gains and test the trajectory envelope separately:

```powershell
py -3 .\Tools\run_commission.py envelope --search acceleration --ceiling 150 --test-degrees 60 --radius 75 --max-trials 5 --out logs\acceleration_session
```

The example ceiling is an experiment parameter, not a rated maximum. The tool
increases only acceleration by 15% per step, repeats each candidate twice and
stops escalating on a failed score or hardware fault. It writes a recommendation
then restores the original RAM setting. It records a baseline trial first and
recommends the qualifying setting with the lowest worst +/-1-degree arrival time, retaining
the baseline if higher limits are slower. `highest_passed` separately records
the largest passing limit. Stability/overshoot checks remain mandatory, and
confirmation time breaks arrival ties. Older reports selected confirmation time;
the 75..150 RPM/s sweep had a 0.9-ms confirmation difference but a 75-ms arrival
improvement. The updated objective matches the stated +/-1-degree positioning
goal. BENCH_TUNING_20261027.md records that completed sweep; it need not be repeated.
For speed use --search speed and an
explicit RPM ceiling. If measured motion never approaches the speed cap, it
stops and explains that a higher cap has not established faster usable motion.
No gains change during envelope search.

The representative mechanism, several positions/directions, temperature trend
and independent motor-case temperature still need final validation. A PCB NTC
and short local experiments cannot establish a continuous thermal maximum or
absolute physical +/-1-degree accuracy with an unqualified weak-field encoder.

## Software evidence and remaining hardware work

Host tests cover actual application one-arm batching, STOP/lease behavior,
stationary-only gain swaps, protocol replay, trace wrap/serialization and true
actuator anti-windup. Loopback UDP tests cover readback/capture/data-loss and the
staged optimizer/rollback. Synthetic-model recovery is tested separately.
All ARM profiles and native Debug/Release builds must pass before flashing.

The trace ring is 2048*88 = 180224 bytes. Pulls contain at most 8 records per
728-byte UDP payload. It is a ring, not unlimited storage; the client detects
overwritten samples. Record capture costs and streaming CPU/ADC timing need
actual-board verification. Software validation does not qualify current-sample
settling at the expanded duty range or the derived controller on this motor.
