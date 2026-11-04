# Validation record, 2026-10-27

Pan hard-stop mapping increment: manual endpoints imply approximately 339.7..339.8
degrees of free travel, with raw angle decreasing CCW. Pan uses a fixed negative
encoder-coordinate sign, separate from calibrated electrical phase polarity;
voltage/current targets and trace voltage polarity follow the mechanical frame.
Pan command limits are 2..337 degrees; oversized network targets and experiment
windows are rejected. Fast effort gating prevents outward requested effort at
the endpoint limits. Tilt calibration-derived coordinates and travel stay intact.
New host tests cover both possible pan phase orders in voltage and current-PI
builds, positive mechanical feedback/effort, endpoints, travel rejection and trace
polarity. Python tests reject pan captures with mismatched bounds/polarity and
out-of-range experiment windows before TEST. Full software/build checks are rerun.
No powered pan test or automatic flash is claimed; pan still defaults to motion
disabled until explicitly enabled for midpoint calibration and a first +10 move.

Persistent motor-profile increment: J3/axis0/TIM1/I2C1 is tilt, J4/axis1/TIM8/I2C2
is pan. Tilt defaults save the user's latest 0.215/18.795/5.5 gains, 110 RPM,
150 RPM/s and 6 V. Pan has independent seed defaults and is motion-disabled by
default because its hard-stop span/polarity remain to be measured. No actual pan
motion or full-range qualification is claimed. Host tests initialize both axis
profiles, confirm reset restores the correct defaults and pan starts inhibited.
Sweep source labels resolve the selected profile. The full software matrix and
native builds are rerun for this firmware configuration change. MOTOR_PROFILES.md
records the midpoint-calibration / clockwise-end-zero preparation and remaining
pan travel/polarity work.

Position objective follow-up: the completed user acceleration sweep passed every
step through 150 RPM/s, improving worst +/-1-degree arrival 0.275 -> 0.200 seconds.
The old selector retained 75 RPM/s on a 0.926-ms confirmation difference. Ranking
now minimizes worst time to enter and remain within the user's +/-1-degree band;
stability and overshoot must still qualify, and confirmation time breaks ties.
Auto, verify and envelope share this objective. All 46 Python tests pass,
including faster arrival with nearly equal confirmation and rejection of fast
arrivals without stability. Firmware and compiled motor defaults are unchanged.
See BENCH_TUNING_20261027.md for measured currents/response and the next experiment.

Position score schema-2 increment: separate arrival, final stable-interval start
and confirmation time; use one 0.5-second dwell and require stability to continue
through the end of a constant-target segment. Tests cover an early temporary
hold followed by target loss and uint32 timestamp wrap. The SET CLI now releases
its owner after inhibited gain writes so the next script can start immediately.
Envelope search measures a baseline and recommends the fastest qualifying worst
settling result rather than the largest passing limit, with regression coverage
for a slower higher limit and baseline retention. All 44 Python tests pass.
Firmware is unchanged. The successful user position/holdout evidence and accepted
local gain pair are recorded in BENCH_TUNING_20261027.md; hardware-wide/thermal
maxima and larger moves remain unqualified.

Python-only explicit position verification: the three user velocity trials
completed without controller faults but all failed the original whole-trial
velocity RMS screen. That screen and its rejected scores remain unchanged.
The verify command can compare an explicitly selected hardware-tested gain
pair against the baseline's position performance, retain position Kp and limits,
require two position holdouts for any improvement recommendation, and restore
baseline settings with acknowledged readback. The report records restoration
and failure evidence. All 38 Python client/model tests pass, including success,
failed position score, fault/restore and rejected limit-change paths. Firmware
images and their earlier build evidence are unchanged. These are local tests;
the new position verification has not yet been run on the mechanism.

Python-only capture follow-up: replay of the user's completed 0.6-V / 50-ms /
1.2-second capture now converges (252 evaluations; original budget was 80), with
0.224 normalized holdout error. The candidate grid now covers positive-Kp
designs for the fitted high damping; quantized gains retain the same phase and
nonlinear simulation screens. Saved completed captures can seed auto with
live build/config/axis matching instead of repeating excitation. Velocity
validation runs eight complete 800-ms cycles. All 34 Python client/model tests
pass, including measured-capture replay, mismatched/incomplete capture rejection,
saved-trace execution and baseline restoration. Firmware sources/images are
unchanged by this follow-up; no flash or board commands were issued. The model
and its candidates have not yet passed closed-loop hardware holdouts.

Commissioning tool increment: voltage duty range .25..75 makes 6 V available at
24 V; current-PI range remains .35..65. A common measured-bus voltage ceiling
drives PWM and velocity anti-windup. Runtime RAM-only parameters have readback,
revision and FNV checksum. SET is prohibited while moving. MCU-timed experiment
modes cover position, velocity, bipolar voltage and balanced PRBS. A single S1
grants a live test batch; STOP/fault/disconnect revokes it. Acquisition samples
at 1 kHz into a 2048*88-byte ring with explicit trace version, trial and sequence.
Host tests cover true-output saturation/reversal, ring wrap, waveform timing,
local travel abort, S1/STOP/lease and actual LwIP SET/TEST/TRACE/replay handling.
Python loopback tests cover readback, dropped traces and staged optimizer/rollback.
Synthetic output-error identification recovers a known delayed/filtered plant
and tests held-out records before deriving/screening gains. The exported plots
were inspected. NumPy/SciPy/Matplotlib live in a workspace-local environment.
No hardware commands or motion were attempted for this increment. The expanded
duty/current window, streaming timing, fitted real model and 6-V operation still
require actual-board validation. New authoritative guide: COMMISSIONING_TOOL.md.
Earlier entries below are commissioning history, not current duty/tool limits.

Keyboard-zero increment: ZERO is queued only in calibrated READY with Ethernet
motion selected, STOP released and no calibration pending. The 1-kHz controller
captures the current accepted encoder coordinate; STOP cancels queued ZERO.
It does not enable PWM or arm position control. The calibration script defaults
to an Enter prompt after electrical calibration has disabled the bridge, then
requires zero=true and position within 1 degree of zero; --skip-zero omits this.
Tests cover actual application gating/capture/STOP, actual LwIP replay/rejection,
and client keyboard flow. Hardware retest required. User's active 100 RPM,
75 RPM/s, position gain 4 tuning was preserved, not increased automatically.

Remote calibration increment: strict CALIBRATE protocol command shares Ethernet
ownership/replay checks, only queues from OFF/READY with motion+Ethernet enabled,
and is consumed by the existing 1-kHz calibration state machine. S2/STOP cancels
queued work and inhibits active work. Remote calibration needs a live 500-ms
command lease; client loss trips COMMAND_TIMEOUT. It clears prior mechanical
zero and never arms position control. HAL/app tests cover blocked modes/running,
STOP priority and lease expiry. Actual LwIP endpoint tests cover acceptance,
replay and other-owner rejection. Fourteen Python client tests pass separately,
including calibration completion/fault/precondition checks. New firmware and
script require assembled-board commissioning; no hardware flash was performed.

Latest requirement audit after a 26-us first-callback SERVO_TIMING capture:
voltage mode uses a 45-us execution budget within the 50-us trigger period;
current PI retains 25 us. The 40..65-us sample-interval and duplicate-sequence
rejections apply only to current PI. Both modes retain 150-us current freshness,
calibrated finite/range-checked currents and per-phase 1.5-A trips. Current-sum
inconsistency stays diagnostic in voltage mode; it trips current PI as before.
Voltage calibration counts settled commanded-vector dwell, then validates
encoder sweep distance, direction and return. Current-vector sign/magnitude
qualification remains current-PI-only. Encoder transport/MD/freshness/jump,
STOP, driver nFAULT, board deadlines, supply/thermal limits, command freshness,
following error, voltage/duty caps and bounded travel remain enforced.
Regression tests accept a 26-us voltage callback and reject a 46-us callback;
controller tests distinguish voltage-mode jitter/sum handling from PI mode and
verify stale and overcurrent shutdown still work. Hardware retest pending.
Older notes below describe earlier commissioning behavior.

Latest voltage-mode behavior: at the user's request, the callback-time timer
window is diagnostic-only when CONTROL_CURRENT_PI_ENABLED=0. Timer-triggered
ADC acquisition, neutral/duty limits and ADC settings are unchanged. Calibration,
freshness, clipping, analog validity, phase-current/current-sum trips, driver
protection and control execution budgets remain enforced. Future current PI
still requires the callback window. Eight acquisition cases cover both axes,
revisions V/Y and both regulation settings (12 BSP executables total). Earlier
notes below describe the investigation before this change. Board retest pending.

Current-window retest captured descending CNT=385, below the 1650..2450
callback acceptance window, with corrected ADC prescaler /2. Native and CLI
Debug builds previously used -O0. CONTROL Debug now uses -O2 with debug symbols
to reduce interrupt and control-path latency; Release settings are unchanged.
Live Expressions globals remain available; optimized local stepping may differ.
This is a latency correction candidate, pending board confirmation. The snapshot
is callback-time, not a hardware timestamp of the ADC aperture. Window limits,
25-us callback budget and all fault protections remain unchanged.

Software checks pass; assembled-board control/Ethernet commissioning is pending.
No image was flashed by the agent. The working BRINGUP project was not changed
by this development increment.

Boot timing fix: a scripted TIM7 interrupt between `board_us()` and the read of
`last_tick_us` reproduced an unsigned-age underflow and false
`FAULT_CONTROL_TIMING`. The watchdog now snapshots both values under one lock.
The regression passes after the fix and still trips for a genuinely stale
control tick. Board timing/foreground faults are no longer mislabeled as
SERVO_DRIVER, and the first servo fault is preserved during shutdown. This fix
does not require an Ethernet cable and does not disable timing protection.

Calibration feedback investigation: explicit tests accept encoder status 0x33
(detected + weak) with the existing commissioning allowance. First-fault captures
now retain the failing input, prior state and encoder-rejection reason, including
ADC freshness/failure masks and errors, even when subsequent readings recover.
Missing magnet and invalid analog data still fault. The 20 kHz PWM-control path
is skipped while inhibited/waking; acquisition and 1 kHz supervision continue.
The captured SERVO_FEEDBACK input identifies ADC1 regular-scan failure with an
accepted encoder. ADC1 stalled at rank 2 of its first frame, with ADSTART set,
EOC interrupt enabled, no overrun and no HAL error. The clock configuration had
not accounted for revision V's additional fixed ADC /2: at 6.25 MHz a regular
rank takes 63.36 us and is starved by the 50-us injected trigger. Prescaler is
now /2 on revision V, /4 on older revisions, giving the intended 12.5 MHz analog
clock, 31.68-us regular rank and 6-us injected frame. Host tests check both
revision paths and both axes. Hardware confirmation is still pending; analog
validation and fault shutdown remain enabled.
Reference: ST RM0433, ADC clocks and triggered injection sections:
https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf

Board retest confirms regular ADC acquisition recovered (fresh mask 7, failed
mask 0, ADC1 count increasing). Calibration now trips SERVO_FEEDBACK in ALIGN:
driver/encoder/analog/zero are good, but the injected current window is invalid.
The first-fault input now preserves current_timer_count, current_timer_cr1 and
current_adc_clock_prescaler from the rejected sample. Tests verify timer values
remain tied to that sample when the live counter changes. No window limits or
protections were relaxed. The reported 41-us max fast callback also exceeds the
25-us drive budget and requires follow-up; it did not replace the first feedback
fault. Successful calibration and motion remain unvalidated.

- Portable controller/protocol tests: motion-disabled voltage mode, active voltage
  mode, and the separately selected future current PI algorithm. Checks include
  both encoder directions, separate mechanical zero, non-wrapping 350->10 travel,
  trajectory acceleration/endpoint behavior, voltage amplitude independent of
  measured current in voltage mode, current-loop feedback sign in the optional
  PI mode, vector/duty saturation, stop, freshness/continuity, command lease,
  supply/temperature/current/sum/timing trips, parser bounds and replay sequence wrap.
- Eight scripted-HAL integration executables: three actual application/button
  configurations, ADC acquisition for both selected axes on revisions V and Y, and the actual Ethernet
  endpoint linked to real LwIP IPv4/UDP. Covers stopped-release calibration,
  immediate STOP, no running re-zero, ADC channel/trigger configuration, rank-paced
  regular scans, all conversions/calibration, injected-current sign and window
  rejection, freshness/error handling, custom RMII pin mapping, PHY identification,
  link transitions, UDP ownership/replay/checksum/stop, and RX pbuf recycling.
- Five Python client tests with real localhost UDP sockets: measured-position
  acceptance, ACK without settling fails, timeout sends DISABLE, status-only query,
  wrong-axis rejection and malformed status handling.
- Six standalone ARM build profiles: Debug/Release inhibited axis0; Debug active
  axis0/pot; Release active axis0/Ethernet, axis1/pot and axis1/Ethernet. Current PI
  is disabled in every production build in this matrix.
- Native CubeIDE clean Debug and Release builds in an isolated IDE workspace:
  zero errors and warnings. Ethernet DMA descriptors/buffers link into aligned
  D2 SRAM and not DTCM. The source copy installed beside the original project is
  checked against the repository development copy.

`validation_evidence.json` records source, ELF and log hashes. Standalone test and
build logs are under `build/validation` in the development repository copy.

These are software tests with scripted hardware boundaries. They do not validate
the physical PHY, clock, ADC sample settling, current scale/polarity, motor gains,
calibration motion, braking/thermal behavior or bounded stopping. Follow
COMMISSIONING.md in order before treating position operation as demonstrated.
