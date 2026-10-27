# Two-motor bring-up

Working single-pan checkpoint: 3323cde on pcb-bringup-h743. Dual changes are
software-validated, not yet qualified on the PCB. Never interpret the checkpoint
as dual-motor hardware validation.

## Build and independent profiles

Set CONTROL_DUAL_AXIS=1 in Core/Inc/pcb_config.h for native CubeIDE Debug.
0 restores single selected-axis operation. CONTROL_MOTION_ENABLED must be1;
Ethernet source, current PI off. PCB_TEST_AXIS selects the legacy debugger
mirror only in dual mode. Both actual controllers use their own profiles.
CLI: py -3 Tools/build.py --config Debug --axis 1 --motion on --source ethernet --dual

- J3 tilt, axis0: TIM1, I2C1, UDP5005; Kp .215/Ki18.795/position5.5,
  110RPM,150RPM/s,6V; calibration1V.
- J4 pan, axis1: TIM8, I2C2, UDP5006; Kp .43/Ki42.3/position5,
  110RPM,199.502RPM/s,6V; <=20deg near-rest cap114.066RPM/s; calibration2V.
- Positive pan is CCW from manual CW-stop zero; commands2..337deg,
  sweep15..320. No shortest-path wrapping.

S1 arms all READY axes with queued valid positions in the same control tick.
Either axis fault, shared nFAULT, physical S2, network link loss or DISABLE
inhibits both. Each axis has its own lease and endpoint owner. No remote ARM,
no automatic rearm. Keep command heartbeats to both while running.
Driver wake/epochs and offsets are independent. Calibration is sequential,
with the other axis inhibited. STOP preserves calibration/zero, MCU reset does not.
The single-axis high-rate TEST/TRACE commissioning tool is intentionally unavailable
in dual mode; normal status/config and runtime SET are available per port.

## After flashing

Calibrate tilt at a suitable free position, then set its mechanical zero at the
Enter prompt with PWM off:

    py -3 Tools/calibrate_motor.py --axis 0 --port 5005

Calibrate pan near mid-travel, then hand-turn CW stop only at the PWM-off zero prompt:

    py -3 Tools/calibrate_motor.py --axis 1 --port 5006

Calibration cannot run while another axis has a queued position or is driving.
The helper's global DISABLE releases the other pending lease; calibration/zero remain.
For a static read: Tools/position_client.py --axis 0 --port 5005 (or axis1/port5006).

First small simultaneous moves:

    py -3 Tools/dual_position_sweep.py --smoke --log-dir logs/dual_smoke

Press S1 once. Defaults target15,25,20,15deg on both. S2/Ctrl-C stops both.
Then the full paired mixed sequence:

    py -3 Tools/dual_position_sweep.py --log-dir logs/dual_sweep

Defaults tilt15..345,pan15..320; two round trips, small5/10deg changes,
medium50deg changes, independently seeded random targets. Commands issued
back-to-back; first physical S1 arms both simultaneously. Each next pair waits
until BOTH have firmware settled, measured error <=1deg and <=.5deg measured
span for an additional .5s. The dwell remains in reported elapsed times.
Polling is ~10..16Hz, not high-rate metrology; peaks may be missed.
--dry-run displays targets without networking. Logs include actual CONFIG readback,
per-axis target/position/RPM/state/fault/settled and laptop elapsed time.

## Live Expressions and acquisition change

Use g_axis_status[0].servo and g_axis_status[1].servo (independent PID state,
commutation calibration and zero); g_app_status mirrors PCB_TEST_AXIS.
Also inspect max_control_us per status and each servo.max_fast_us/current_age_us.
ADC1 alternates three-rank injected contexts at TIM1's20kHz trigger: each axis
receives10kHz current feedback and its own fast voltage-commutation callback.
Both PWM carriers remain20kHz and free-running, aligned while outputs are off
with consecutive CEN writes (small APB-write skew, not hardware zero-skew sync). ADC context changes happen
after JEOS, using the vendor HAL's documented runtime-writable JSQR fields.
Both current channel sets use16.5cycle sampling; regular diagnostics continue.
This new acquisition path requires physical timing/current validation on the PCB;
Current freshness is three per-axis sample periods: 300us in dual mode,
150us in single mode. The45us callback budget and validity/current/fault
checks remain enforced. A frozen valid158us frame exposed the original
single-axis threshold left behind in dual mode.
Shared nFAULT is PE6 EXTI in dual mode; either fault disables BOTH MOE/ENABLEs.

## Temporary software phase-current magnitude bypass

User requested continued voltage-mode bring-up while current sampling is investigated.
CONTROL_SOFTWARE_PHASE_TRIP_ENABLED defaults to0 for dual builds,1 for single builds.
This disables only the software +/-1.5A magnitude comparison. STATUS exposes
phase_trip_enabled. ADC failure, nonfinite readings, current freshness and zero
calibration checks, driver nFAULT, hardware driver protection, STOP and watchdogs
remain active. Current PI cannot compile with the magnitude trip disabled.
Restore the flag to1 after current sampling is qualified. This temporary build
has reduced overcurrent protection; retain the existing voltage/motion limits
and do not use its suspect current telemetry to increase loading or voltage.
