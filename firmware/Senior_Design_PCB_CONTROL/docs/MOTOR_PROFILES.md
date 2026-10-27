# Tilt and pan profiles

| Axis | Connector | Motor | PWM | Encoder |
|---:|---|---|---|---|
| 0 | J3 | Tilt | TIM1 | I2C1 |
| 1 | J4 | Pan, with physical hard stop | TIM8 | I2C2 |

Only one selected axis is driven at a time. `PCB_TEST_AXIS` in
`Core/Inc/pcb_config.h` selects the firmware axis. Python `--axis` verifies that
selection; it does not switch the firmware to the other motor.

## Persistent tilt defaults

Edit `Core/Inc/motor_profiles.h`. The TILT_ constants contain the user's latest
accepted sweep values: velocity Kp 0.215, velocity Ki 18.795, position Kp 5.5,
max speed 110 RPM, acceleration 150 RPM/s, and phase voltage amplitude 6 V.
`control_config.h` includes that header and retains shared driver/protection
settings. Build and flash for changes to become reset defaults. The Ethernet
SET command only changes the running RAM copy; it never edits this file or
writes persistent flash. A reset/new image requires electrical calibration and
mechanical zero again.

J3 remains selected in the project. Its saved defaults are not affected by later
pan SET commands or PAN_ profile edits. The preceding measured reports used
position Kp 5 and 100 RPM; 5.5 and 110 are the user's subsequent accepted settings.

## Pan starting point and hard stop

PAN_ values are separate, untuned seeds: velocity Kp/Ki 0.215/18.795, position
Kp 5, max speed 100 RPM, acceleration 75 RPM/s, amplitude 6 V. The different
mechanical load still needs its own capture and position validation. Never
reuse the tilt identification CSV as a pan plant model; the tool checks axes.

Selecting axis 1 defaults `CONTROL_MOTION_ENABLED` to zero, allowing encoder
inspection before motor driving. An explicit compile definition can override
this, but neither changing axis nor flashing the supplied default enables pan
motion. No automatic motor motion, calibration or flash is performed by tooling
installation.

Requested pan coordinates: clockwise physical end = 0 degrees, increasing
position = counterclockwise free travel. The measured encoder sign and calibrated
phase order now implement that convention in separate coordinate transforms.
The first short powered move must still confirm the hardware behavior.
Electrical calibration must start clear of both stops:
alignment/sweep/return can move either way. Only after calibration finishes with
PWM off should the user move to the clockwise stop and press Enter to capture
mechanical zero.

Manual measurements received: clockwise angle/unwrapped position 319.5 degrees;
counterclockwise angle 339.7 degrees / unwrapped position -20.2 degrees. The user
confirmed raw angle decreases counterclockwise. Unwrapped travel is approximately
339.7..339.8 degrees, not the 20.2-degree gap between wrapped endpoint angles.

The pan profile now fixes the mechanical encoder sign at -1, independently of
electrical phase order. Electrical calibration still learns phase polarity;
torque commands and current targets are transformed to the requested mechanical
coordinate. Both possible phase orders are tested. The diagnostic raw feedback
position can still be negative; the selected servo's `position_deg`, after zero,
is the clockwise-end-relative positive-CCW coordinate.

Pan commanded positions are inset to 2..337 degrees. Network requests above
337 degrees are rejected before accepting motion. Trajectory targets, outward
endpoint effort and experiment windows use these bounds. CONFIG reports
min_mdeg=2000, max_mdeg=337000 and encoder_sign=-1. Old 358-degree sweep commands
must not be used on pan. The mechanical-span estimate is 339.8 degrees; these
software margins are not a guarantee against inertia or an incorrect zero.

## First powered pan steps

J3 remains selected in the project. To build pan in CubeIDE, change
`PCB_TEST_AXIS` to 1 and explicitly set `CONTROL_MOTION_ENABLED` to 1 in
control_config.h. Build/flash after changing these. Alternatively, the prepared
CLI axis-1 motion-on ELF has those build overrides already. Both boot with gates
off and need calibration and separate arming; no image is flashed automatically.

1. With drive off, move pan near mid-travel, well clear of both stops.
2. Run `py -3 Tools/calibrate_motor.py --axis 1`. Alignment/sweep/return can move
   either way; do not start electrical calibration at a hard stop.
3. At its Enter prompt PWM is off. Move to the clockwise stop, release the shaft
   and press Enter to capture zero. Direction is mapped CCW-positive by firmware.
4. First test `py -3 Tools/position_client.py --axis 1 --position 10`. Press S1.
   Confirm it moves away from the clockwise stop and stops near +10 degrees.
5. Then move to a safe middle position, collect a new pan voltage capture, and
   repeat local position/holdout validation with pan-specific log directories.

The controlled pan mapping and bounds are software-tested, not yet verified by
a powered pan move. Pan gain seeds remain untuned. A later full pan sweep should
stay inside the measured limits (for example 5..335 degrees), after the initial
direction, zero and local loop checks have passed.
