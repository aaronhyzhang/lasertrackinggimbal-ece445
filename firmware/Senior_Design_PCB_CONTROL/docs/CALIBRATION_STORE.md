# Calibration storage and homing (dual-axis build)

Design doc 2.3.4: store encoder zero offsets and calibration constants in non-volatile
memory and validate them by checksum on boot. Files: `Core/Inc/cal_store.h`,
`Core/Src/cal_store.c` (portable record/restore), `Core/Inc/cal_flash.h`,
`Core/Src/cal_flash.c` (STM32H7 flash), homing in `position_control.c`, integration in
`demo_dual.inc`. **Software-tested only; not yet run on the PCB.**

## What is stored

Per axis, after it is established on the board:

| Item | Source | Valid across power cycles when |
|---|---|---|
| Electrical calibration: `direction`, `electrical_offset` | CALIBRATE (align, sweep, return) | Magnet and encoder are not remounted. It is tied to the encoder reading, not the shaft position |
| Mechanical zero: AS5600 count at 0 degrees | ZERO (by hand) or HOME (pan hard stop) | Restorable only if exactly one whole-turn choice puts the shaft inside travel (below) |

The record is 64 bytes: magic, version, sequence, a configuration ID (pole pairs, encoder
signs, travel limits) and a CRC-32. A record with a bad CRC, wrong version or different
configuration ID is ignored, never trusted. Gain/tuning values are not stored; they come from
`motor_profiles.h` as before.

## Flash layout

Last 128 KB sector of bank 2: `0x081E0000`, sector 7. The linker script now gives the
program 1920 KB, so code cannot be placed there (current image: about 75 KB). Records are
appended in 64-byte slots (2048 per sector); the sector is erased only when full.

Writes happen in the foreground loop, **only while both bridges are idle**. A sector erase
is started without waiting and finished by later foreground passes, so the watchdog,
foreground-age check and control interrupt are never blocked. Code runs from bank 1, so a
bank-2 erase does not stall it. Each program step is two 256-bit flash words, verified by
read-back. Failures set `flash=2` in STATUS and stop further writes until reset.

## Boot restore

Once per axis, after its first accepted encoder sample, an OFF and uncalibrated axis is
restored from the newest valid record:

- Electrical calibration restored → axis READY (PWM stays off; nothing moves).
- Zero restored only if enabled for that axis **and** exactly one whole-turn offset puts the
  current reading within travel ±2 degrees:
  - **Pan** (`CAL_RESTORE_ZERO_PAN 1`): travel is about 339.8 degrees between hard stops,
    under one turn, so the position is unique. A reading in the unreachable gap restores no zero.
  - **Tilt** (`CAL_RESTORE_ZERO_TILT 0`): disabled until its mechanical travel is confirmed to
    be under one turn. Set to 1 then; ambiguous readings near the ends are still refused.
- Restoring never rewrites flash.

Saves happen when RAM differs from the stored record: a new electrical calibration (which
also voids that axis's stored zero, since CALIBRATE clears the RAM zero) or a new zero. Glitches
that drop the RAM zero never erase a stored one.

## Homing (HOME)

Pan only (`PAN_HOME_DIRECTION -1`, `TILT_HOME_DIRECTION 0`), voltage mode only.

1. `G1 ... HOME` on port 5006 queues it (needs: READY, electrically calibrated, no target
   queued, other axis idle). Heartbeats must continue; an unpressed request lapses with the lease.
2. **Physical S1** starts it: driver wake, then a 5 RPM (30 deg/s) velocity loop toward the
   clockwise stop, effort capped at 2 V (`CONTROL_HOME_VOLTS`).
3. A stall (saturated push toward the stop, under 0.5 RPM, for 300 ms after a 500 ms
   settle) sets that position as 0 degrees. The axis returns to READY with PWM off.
4. If a zero is already stored and the homed zero differs by more than 3 degrees
   (`CAL_HOME_TOLERANCE_DEG`), the axis faults (SERVO_CALIBRATION) instead of using it: a
   false stall (friction, cable) or changed mechanics. Run FORGET and home again if the
   mechanics really changed.

Every servo protection stays active while homing (feedback, driver, current, supply,
temperature, timing, lease). No stop found within `CONTROL_HOME_TIMEOUT_MS` (about 13.5 s)
faults with bounded travel. S2 stops it. The servo reports state 9 (HOMING), counted as
CALIBRATING by the system state machine.

## Commands and STATUS

| Command | Effect |
|---|---|
| `G1 <s> <q> HOME` | Queue homing on that port's axis; S1 starts it |
| `G1 <s> <q> FORGET` | Erase stored and RAM calibration on both axes (only when nothing is active or queued); writes an empty record |

Both may acquire an idle endpoint like CALIBRATE; owner and sequence rules apply.
New STATUS fields (dual build): `stored` (0 nothing, 1 electrical, 2 electrical+zero),
`restored` (same scale, what boot restored), `save_pending`, `flash` (0 idle, 1 erasing,
2 error), `home_supported`. Live Expressions: `cal_saved`, `cal_restored`, `cal_dirty`.

Single-axis builds compile the code but do not use it; HOME and FORGET reply `ERR`.

## Tests

| Test | Runner | Covers |
|---|---|---|
| `test_cal_store.c` (2 builds) | `Tools/test.py` | CRC (incl. every single-bit flip), validation, log scan with corruption and sequence wrap, capture, pan restore across travel, gap/guards, tilt ambiguity |
| `test_homing.c` | `Tools/test.py` | Simulated pan with a hard stop: wake, move, stall = 0, coordinate check, re-arm, already-at-stop, no stop → timeout, lease loss, rejections, STOP, endpoint bypass |
| `test_cal_flash.c` (32-bit) | `Tools/test_bsp.py` | Actual `cal_flash.c` with mapped registers: append, reboot scan, non-blocking erase when full, busy refusal, program/verify/erase errors |
| `test_dual_calibration.c` | `Tools/test_bsp.py` | Actual `demo_dual.inc`: boot restore, save only when idle, power cycle, FORGET, HOME via S1, stored-zero mismatch fault, lapse, tilt refusal |
| `test_network.c` | `Tools/test_bsp.py` | HOME/FORGET through real LwIP: acquire, replay, owner, syntax |

Verified 2026-10-27 with zig 0.16.0: all of the above plus every existing suite pass;
`Tools/build.py` dual Debug/Release and single Debug/Release build with `-Werror`; image ends at
0x080123D0. Two injected bugs (write while active, trusting a mismatched homed zero) are caught.
Only `test_client_commission.ModelTests` fails, because SciPy is not installed (unrelated).

## Bench checklist (first time on the PCB)

1. Flash the dual build. STATUS on both ports: `stored=0 restored=0 flash=0`.
2. Calibrate both axes (laptop `gimbal_bridge.calibration`). Expect `stored=2` on pan after HOME
   and `stored=2` on tilt after the hand zero.
3. Power cycle. Expect pan `restored=2 zero=1` at the correct angle (check against a mark), tilt
   `restored=1 zero=0`. Move pan by hand while off, power on, check the angle again.
4. HOME pan a second time: must succeed without a mismatch fault.
5. Watch for a false stall: if homing stops short of the stop, raise `CONTROL_HOME_VOLTS` slightly
   or fix the friction; never widen `CAL_HOME_TOLERANCE_DEG` to hide it.

Known limits: a power loss in the microseconds of a flash-word write can leave an ECC-corrupt
slot; reading it may fault at boot. Recover by erasing sector 7 of bank 2 (STM32CubeProgrammer)
and recalibrating. Remounting a magnet requires FORGET.
