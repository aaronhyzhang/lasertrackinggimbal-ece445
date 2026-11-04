# Pan move-dependent acceleration trial, 2026-10-27

Pan reset checkpoint: velocity Kp 0.43, Ki 42.3, position Kp 5,
max RPM 110, normal acceleration 199.502 RPM/s, run amplitude 6 V.
Tilt defaults are unchanged. Calibration amplitude remains the user's 1.5 V.

For pan targets up to 20 degrees from both measured position and trajectory,
select min(runtime acceleration,114.066 RPM/s) when the target changes,
provided trajectory and measured speed are both at most 2 RPM.
Latch the selection until another effective target or STOP/re-arm.
Moving retargets retain normal acceleration. Heartbeats do not restart selection.
This policy adds no delay and does not change velocity PI or position gains.
The 20-degree threshold is an initial experiment, not a qualified optimum.
Set PAN_SMALL_MOVE_DEG to 0 to disable the policy.

Live Expressions: g_app_status.servo.move_distance_deg, move_target_deg,
move_accel_rpm_s, move_profile_valid. Runtime config accel_rpm_s remains the
normal cap; CONFIG/trace schemas remain unchanged. Use actual latched telemetry
when evaluating short moves, not the CONFIG acceleration alone.

After rebuild/flash, redo commutation calibration near mid travel, then zero
at the CW stop with PWM off. Repeat small/medium/large captures and compare
arrival, final stability and overshoot. No hardware tests performed by the agent.
