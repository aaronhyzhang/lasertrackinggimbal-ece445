# Commissioning sequence

This is a new development image. Keep the known-working BRINGUP image available.
Start with one unloaded motor; the other bridge remains off. All numbers below
are initial test limits, not demonstrated motor/system maxima.

## 1. Sensor and network checks with motion disabled

Flash the default `CONTROL_MOTION_ENABLED=0` build. Check `g_app_status.test_axis`,
`motion_enabled=false`, `current_regulation=false`, fresh encoder/analog values,
and the independent encoder-to-shaft association. The driver's CSAs cannot
calibrate while the driver remains asleep; use the known bring-up image's READY
calibration result as prior evidence, then check new calibration when waking here.

Check `g_network`: expected PHY ID1=7, ID2&0xfff0=0xc130, address 0. LINK_UP follows
autonegotiation. Run the Python client without `--position` to read STATUS. This
proves the network command round trip with outputs inhibited; it does not prove
motor positioning. If MAC initialization fails, first check 50 MHz REFCLK at PA1.

## 2. Current-window and commutation qualification

Build Release with motion enabled, source=pot, current PI disabled. Put the shaft
well away from either travel boundary, near the physical middle of its turn.
Initial alignment may move to a nearby electrical equilibrium, followed by an
approximately 8.18-degree mechanical sweep and return. Do not calibrate at a stop.
Calibration cannot discover a hard stop or establish the one-turn origin.

Hold S2 for at least two seconds **while already stopped**, then release it.
The driver wakes with gates off, verifies the common DRV8304 profile (20 V/V),
and collects 256 stable offsets. It waits at least 350 ms; failed calibration
times out after two seconds without applying PWM. Then it applies a 1 V d-axis
alignment with a 500 ms ramp, sweeps an electrical quarter-turn and returns.
The complete sequence takes about 7.35 seconds. Current direction relative to
the static vector, encoder direction and movement magnitude must qualify.

Observe the first run on the scope and in Live Expressions. S2 remains immediate
STOP throughout. Confirm the selected phases move in the expected order, no
driver fault occurs, calibration ends at SERVO_READY (5), and
`commutation_calibrated=true`, `zero_set=false`.

Current acquisition is ADC1 injected A/B/C, selected by TIM1 or TIM8 TRGO2.
CH4 PWM2 rises at CNT2300 on the upward count of a 2500-count, 20 kHz carrier.
ADC clock is 12.5 MHz; sampling is 16.5 cycles, conversion approximately 8.5
cycles per channel, total approximately 6 us and A-to-C skew approximately 4 us.
Duties are limited to .35..65, leaving a common low-side interval. The callback
requires descending count and CNT1650..2450. Slow regular sequences advance one
rank per trigger after DR has been consumed, so fast-current work cannot overwrite
a following regular rank. There is one running-duty writer, the injected callback.

These are designed windows, **not measured settling evidence**. With a suitable
probe, verify low-side conduction and quiet CSA output throughout the sample
window, including at the largest requested duty. Measure loop execution/jitter
with Live Expressions and Ethernet active. `max_fast_us` must remain below the
25 us software ceiling; period/window/ADC errors must remain absent. Validate
amperes/polarity against an independent current measurement. Samples are not
automatically winding RMS current, particularly during transients.

## 3. Mechanical zero and first position tests

With SERVO_READY and PWM off, manually place the shaft at the intended physical
zero. Hold S2 two seconds and release again. This captures a separate mechanical
origin: `zero_set=true`, `position_deg=0`. It does not redo electrical calibration.
Positive position is the direction identified by the positive calibration sweep;
verify a hand movement in that direction increases the coordinate. Avoid making
an extra turn while manually setting the origin.

Set VR1 near zero and press/release S1. Then test small commands (for example
10, 20, 40 degrees) and S2 before testing larger moves. VR1's nominal 0..3.3 V
maps to requested 0..360 degrees, with no wrap. Applied targets initially clamp
to 2..358 degrees to reserve an endpoint margin. A 350->10 command follows the
long path back through the interval. Inspect `requested_deg` versus `target_deg`.

Initial limits: 30 RPM, 30 RPM/s, 2.5 V peak quadrature voltage; 1.5 A sampled
phase-current trip, 18..28 V supply window, 60 C local PCB-temperature trip.
The board NTC cannot protect unmeasured motor winding temperature. Measure motor
case temperature independently. These are provisional commissioning settings.
The position gain is 3/s and the velocity PI uses 0.5 V/(rad/s), 0.5 V/rad.
Gains need physical tuning; voltage mode does not regulate current or torque.

S2 inhibits and releases torque. Feedback loss, stale samples, current/sum error,
excess following error, out-of-range travel, supply, temperature and timing faults
also inhibit. No packet or pot change clears a fault. Missing single-turn encoder
continuity invalidates the mechanical zero. Mechanical limits cannot be guaranteed
by software alone: inertia can carry the shaft after inhibition. Keep the endpoint
margin until real stopping/overshoot measurements justify changing it.

## 4. Ethernet motion validation

After pot-position operation passes, select `CONTROL_SOURCE_ETHERNET=1`, keeping
current PI disabled. Reflash, repeat electrical calibration and mechanical zero,
and leave the controller READY. Connect the laptop and run:

```text
python Tools/position_client.py --axis 0 --position 90
```

The script first verifies the selected axis, Ethernet source, enabled build,
calibration and zero. POSITION sets a target but does not energize the bridge.
Press physical S1 when prompted. Confirm measured position settles and the script
prints PASS, then DISABLE. Start with a small move before 90 degrees on constrained
mechanics. Test S2, cable unplug, client exit and stopped heartbeats; all must
inhibit, and reconnecting must not automatically rearm. The lease is 500 ms;
status queries do not refresh it. Owner identity includes IP, port and session,
and command sequence numbers reject old/duplicate commands.

## 5. Later FOC stage

Keep `CONTROL_CURRENT_PI_ENABLED=0` until encoder-oriented voltage commutation,
current channel mapping/sign/gain, acquisition timing and stopping have passed.
The prepared optional d/q current PI path is not hardware-qualified. Its initial
q-current ceiling is 0.5 A, d-current target zero, and voltage-vector ceiling 3 V.
That path requires separate current-loop tuning before repeating velocity and
position tuning. Do not copy open-loop high-speed limits into this controller.
The AS5600's existing filter and 100 kHz read latency remain unchanged; they limit
commutation bandwidth. No OTP configuration is written.

## State and fault decoding

States: 0 OFF, 1 WAKE_CAL, 2 ALIGN, 3 SWEEP, 4 RETURN, 5 READY, 6 WAKE_RUN,
7 RUNNING, 8 FAULT. Faults: 0 OK, 1 DRIVER, 2 FEEDBACK, 3 ZERO_TIMEOUT,
4 CALIBRATION, 5 CURRENT, 6 CURRENT_SUM, 7 TRAVEL, 8 FOLLOWING,
9 COMMAND_TIMEOUT, 10 SUPPLY, 11 TEMPERATURE, 12 TIMING, 13 FOREGROUND,
14 INTERNAL. `g_app_status.board_fault` preserves the underlying board fault;
`control_age_us` and `foreground_age_ms` expose the independent timing checks.

The fixed encoder's weak-field warning remains visible (`weak_magnet=true`).
The user requested proceeding despite the unadjustable placement. This is an
explicit commissioning allowance for MD-present data, not evidence of specified
angle accuracy. Disconnects, missing MD, stale data and implausible jumps still trip.

For SERVO_FEEDBACK, inspect `g_app_status.servo.trip_state`, `trip_encoder_reject`
and expand `trip_input`. They capture the first failed sample and remain fixed
even if live readings recover. ENCODER_ACCEPTED means magnet/transport/continuity
checks passed at the control-tick trip; then check `analog_valid`, `adc_fresh_mask`,
`adc_failed_mask`, `adc_errors`, `current_valid` and `current_window_valid`.
ADC mask bits 0/1/2 correspond to ADC1/ADC2/ADC3. A healthy fresh mask is 7 and
failed mask is 0. `CONTROL_ALLOW_WEAK_MAGNET=1` explicitly accepts MD-present weak
data; the controller does not use diagnostic `encoder.valid`/`magnet_ok` to reject
that data. Inhibited/waking states skip the 20 kHz PWM-control workload; ADC
acquisition and the 1 kHz feedback prerequisites remain active.
