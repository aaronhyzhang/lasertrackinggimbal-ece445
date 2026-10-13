# Open-loop speed bring-up and mode selection

Updated 2026-10-12. Firmware v0.2.0. One unloaded FIT1036 motor, NUCLEO-H753ZI, BOOSTXL-DRV8304H, configured 24 V bus. Software tests/builds pass; actual waveforms, current, alignment and motor synchronization remain unverified.

## How much simpler is open loop?

The spinning algorithm needs a target speed, an acceleration ramp, an angle accumulator and a bounded voltage vector. It bypasses rotor-angle feedback, sensor electrical calibration, mechanical zero, unwrapped position tracking, position P, velocity PI and controller anti-windup. ADC acquisition, six-PWM generation, deadtime, driver wake/fault handling, stop interlocks, watchdog and diagnostics are still needed.

| Piece | OPEN_LOOP | CLOSED_LOOP |
| --- | --- | --- |
| Analog input | Commanded one-way speed | Bounded 0–360° position |
| Electrical angle | Generated from elapsed nominal control ticks | Encoder × pole pairs + calibrated offset |
| Position / velocity regulators | Bypassed | Position P and velocity PI |
| Encoder required | No | Yes, with magnet/freshness checks |
| Alignment | Timed stationary field, without confirmation of rotor movement | Alignment, measured sign sweep, return and offset validation |
| Calibration and ZERO commands | Rejected | Required before ARM |
| Mechanical travel limit | No full-turn limit; continuous bench spin | One-turn position reference and travel/tracking fault checks |
| Stall / actual speed detection | Not provided | Uses encoder feedback and following-error checks |
| Current regulation | Not provided | Not provided in this voltage-mode demo; current FOC is later |
| Network motion | Rejected; analog-only open loop | Existing selected-source position protocol |

The core angle update is:

```text
speed_reference approaches speed_target at the configured acceleration
electrical_angle = wrap(electrical_angle + 11 * speed_reference_rad_s * 0.001 s)
phase_duties = sine_modulation(electrical_angle, bounded_voltage_vector)
```

`Core/Src/open_loop.c` contains this simpler path and its timed startup states. It reuses the existing board, modulation and fault helpers. Both modes live in one maintained source tree, so the combined image is larger than the earlier closed-loop-only image even though the open-loop control path is simpler. No second copy of HAL or the application is maintained.

Open loop commands the rotating stator field. The rotor follows only if alignment, voltage, acceleration and load permit synchronization. The reported commanded/ramped RPM is **not a measured motor speed**. A stalled motor can continue receiving excitation. This mode is for initial unloaded bench validation, not gimbal position/holding or final tracking.

## Analog mapping and defaults

The previously reserved **PA3 / ADC1_INP15** remains the analog input. The measured signal uses `DEMO_ADC_VREF` (default 3.3 V); **3 V is the command full scale**, not ADC reference voltage.

| Input | Nominal commanded mechanical speed | Electrical field frequency, 11 pole pairs |
| --- | --- | --- |
| 0 V | 0 RPM, coast once the speed ramp reaches zero | 0 Hz |
| 0.75 V | 7.5 RPM | 1.375 Hz |
| 1.5 V | 15 RPM | 2.75 Hz |
| 2.25 V | 22.5 RPM | 4.125 Hz |
| 3 V | 30 RPM | 5.5 Hz |

The 30 RPM maximum is a provisional configurable bring-up choice, not the FIT1036 rated speed or a user-confirmed performance requirement. Adjust `DEMO_OPEN_MAX_RPM` after review/bench evidence. Above the zero deadband, speed = `V/3 * max_RPM`. A 0–30 mV deadband commands zero to avoid ADC noise starting a motor. Small readings from 3.0 to 3.15 V clamp to the maximum; above 3.15 V, negative/nonfinite values or stale/invalid ADC data fault while armed. Electrical input protection still has to keep PA3 within its supply limits.

Defaults in `Core/Inc/demo_config.h`:

- Maximum command: **30 RPM**, positive only.
- Acceleration/deceleration: **10 RPM/s**; full-scale speed ramp takes approximately 3 seconds after alignment.
- Open-loop rotating vector: **2 V** magnitude, using generated d voltage and zero q voltage. These are generated-frame coordinates, not rotor-oriented torque/current measurements.
- Startup alignment: **1 V**, ramped over 0.5 s and held within a total 2 s timed stage.
- Running voltage slew from alignment to 2 V: **2 V/s**.
- ARM requires input in the **≤30 mV zero-speed region**; calibration/ZERO are not required in this mode.
- Driver wake: **10 ms**, PWM inhibited; fault must be high before alignment.
- ADC, foreground, external driver faults and control timing/watchdog checks remain active. Encoder faults/health do not gate open-loop drive.

The voltage limit is not a phase-current limit. Keep a current-limited bench supply and measure current/temperature. The 24 V configured bus is not measured by this profile. Loss of synchronism, unknown phase resistance, wrong driver straps or an actual bus different from the configured bus invalidate assumptions about applied voltage/current.

## Separate open-loop-default firmware image

From `Senior_Design_DEV_FW`:

```powershell
python Tools/build.py --profile open-loop --config Debug
python Tools/build.py --profile open-loop --config Release
```

Images are emitted under:

```text
build/OpenLoop/Debug/Senior_Design_DEV_FW.elf   (also .bin/.hex/.map)
build/OpenLoop/Release/Senior_Design_DEV_FW.elf (also .bin/.hex/.map)
```

These builds define `DEMO_DEFAULT_OPEN_LOOP=1`. They boot with mode OPEN_LOOP and state DISABLED. They still support the closed-loop mode toggle. A compile-time default is not permission to energize at boot.

The normal build / native CubeIDE Debug and Release configurations boot CLOSED_LOOP, but support `MODE OPEN_LOOP`:

```powershell
python Tools/build.py --profile closed-loop --config Debug
python Tools/build.py --profile closed-loop --config Release
```

Normal outputs remain under `build/Debug` and `build/Release`. To boot open loop from a native CubeIDE build, add the symbol `DEMO_DEFAULT_OPEN_LOOP=1` to **all** C compilation units in the selected configuration and rebuild; the CLI profile avoids changing the IDE configuration. Program with motor supply disconnected. Hardware has not been flashed by these build commands.

## Console sequence

ST-LINK VCP remains **115200, 8N1**. Commands use uppercase and CR/LF termination.

```text
DISABLE
STATUS
MODE OPEN_LOOP
STATUS
ARM
STATUS
```

Wait for DISABLED/READY after DISABLE and for `mode=OPEN_LOOP state=DISABLED` after MODE. Actions and mode changes report QUEUED; STATUS shows their applied result. Put the analog source at **0 V before ARM**. ARM at a higher input is ignored by the control prerequisites and leaves the motor disabled. Successful ARM yields OPEN_IDLE with PWM and ENABLE off. A raised analog input begins OPEN_WAKE -> OPEN_ALIGN -> OPEN_RUNNING.

OPEN_IDLE is logically armed even though it coasts. It refuses mode/source changes because a later analog increase could start excitation. Use DISABLE first. MODE changes are accepted only in DISABLED/READY, never during wake/alignment/run or FAULT. Stop cancels pending ARM and pending mode changes. MODE resets calibration/zero, integrators, generator state and command authority and selects ANALOG; it never energizes a motor.

At a 0 V command during running, commanded frequency ramps down at 10 RPM/s, then ENABLE/PWM are inhibited and state returns to OPEN_IDLE. This may take about 3 seconds from 30 RPM. There is no energized zero-speed hold or controlled physical braking; the actual rotor coasts. Returning to a nonzero input while still armed performs another wake/alignment before spinning. Zero during wake/alignment cancels excitation immediately. **DISABLE or `!` always inhibits immediately**, without waiting for the analog speed ramp.

To return to the existing position demo:

```text
DISABLE
STATUS
MODE CLOSED_LOOP
STATUS
CALIBRATE
STATUS
ZERO
ARM
```

Before CALIBRATE, verify the encoder is connected, configured and healthy. Wait for successful READY after calibration, set the intended mechanical-zero fixture orientation with drive off, issue ZERO, then ARM. The analog input again means 0–360° position. Previous open-loop angles or commands never become position setpoints. The full procedure/limits are in [FIRMWARE_GUIDE.md](FIRMWARE_GUIDE.md).

`SOURCE SERIAL/ETHERNET`, POSITION and motion HEARTBEAT are rejected in open loop. Network STATUS and DISABLE remain diagnostic/stop operations; no network ARM or MODE operation was added. This prevents an old Ethernet position packet from being interpreted as a speed. `CLEAR_FAULT` in open loop requires healthy analog/liveness inputs but no encoder, returns to DISABLED and still requires a new near-zero ARM. Switching modes cannot clear a fault.

STATUS includes `mode`, `cmd_mrpm` (command RPM ×1000), `ramp_mrpm` (generated RPM ×1000), and `open_mV` (vector magnitude). Existing `uq_mV`, position/reference/velocity fields are for the closed-loop controller. `pos_valid=0` in open loop; raw encoder/magnet/age fields can help wiring checks, but they are not a validated open-loop mechanical speed/position estimate.

## Step-by-step acceptance

1. **No motor power:** verify VCP, default mode and DISABLED at boot, meter-match ADC points, confirm MODE refuses ARM when input is nonzero. Confirm the active project/image version and profile.
2. **EVM powered, winding leads disconnected:** verify hardware straps and logic rails, then select OPEN_LOOP, ARM at zero and raise analog. Scope all six signals, nominal 20 kHz carrier, deadtime, coherent phase updates and generated field frequency. This does not require an encoder. At full scale the rotating phase envelope is 5.5 Hz for the provisional 30 RPM /11-pair configuration.
3. **Shutdown tests, no winding connection:** verify nFAULT during wake versus active drive, `!`, DISABLE, ADC loss/invalid input, reset and foreground/watchdog behavior. Record physical shutdown latency/inactive states, not just status text.
4. **Power off, attach secured unloaded motor:** verify continuity/phase identity, set 0 V, current-limit the 24 V supply, ARM, then start with a small voltage/speed command. Observe alignment, smooth startup, physical direction and current. Progress through 0.75/1.5/2.25/3 V only after the lower point works.
5. **Speed and coast:** measure actual shaft RPM using the encoder as an independent observation tool or external tachometer, compare against the commanded/ramped values, and record any stalls or loss of synchronism. Verify deceleration to zero and immediate disable separately; inspect current/temperature. Do not raise voltage/acceleration to hide wrong phasing.
6. **Add encoder feedback:** with drive inhibited, validate encoder supply, SDA/SCL pulls, hand rotation, magnet status and sample age. If it was absent at boot, repair/attach with power off and reboot for its configuration/readback before trying closed loop.
7. **Toggle to closed loop:** perform new electrical calibration and mechanical ZERO, then repeat the earlier bounded position milestone. Ethernet position validation follows after that succeeds.

The firmware's OPEN_ALIGN completion only means its timer elapsed. It does not prove the rotor reached the intended alignment. If the rotor chatters or does not start, disable and inspect phase assignment, power/driver faults, actual bus/current, mechanical load and limits. Automatic stall recovery or encoder-based trim is not part of this test mode.

## Hardware assumptions to verify and correct

| Assumption | Basis / pending evidence |
| --- | --- |
| One motor/inverter, FIT1036, 11 pole pairs | User-confirmed SKU and DFRobot specification; verify received motor/phase identity |
| Bus 24 V, voltage-vector scaling uses that value | User-selected nominal bus; meter actual bus, note supply collapse/regeneration; no firmware bus/current measurement |
| DRV8304H in **6-PWM** mode | Existing six-output BSP; inspect MODE strap at AGND and CAL at ground; a 3-PWM strap is incompatible |
| Same six-input/ENABLE/nFAULT harness | [BOARD_CONFIG.md](BOARD_CONFIG.md); continuity and signal polarity/route require scope evidence |
| Positive electrical phase sequence produces the intended one physical direction | Actual CW/CCW depends on A/B/C wiring and viewing side; record direction; swap any two winding leads only with power off if needed, then recalibrate for closed loop |
| 3.3 V digital interface | VDDIO and nFAULT pull-up at 3.3 V; common signal ground; ENABLE effective pull-down for reset/unpowered MCU |
| PA3 is a protected 0–3 V source, not the motor bus | Verify source range/reference/grounding and effective pull-down; unplug may look like a valid zero command |
| Encoder can be omitted for the spin test | No open-loop feedback dependency; PB6/PB7 external pulls to 3.3 V or verified encoder-board pulls are still needed when adding I2C |
| Unloaded shaft with continuous rotation clearance | No gimbal travel/cable/position limits in open loop; verify fixture, lead routing and absence of mechanical stops |
| 1 V alignment /2 V run /30 RPM /10 RPM/s are sufficient and bounded for first motion | Engineering starting choices, not measured ratings, torque or a 4 A trip; log actual synchronization, current and temperature |
| EVM shunts, GAIN, IDRIVE, VDS protection configuration are independently verified | No firmware setting/readback/current sampling in this H voltage-mode backend; do not import custom PCB 14 mΩ/gain assumptions |

Write actual strap values, phase order/viewing direction, measured bus/VDDA, supply limit, RPM, current/temp, scope captures and corrected limits in [VALIDATION.md](VALIDATION.md). Keep unknowns explicit instead of treating an elapsed startup timer as proof of hardware correctness.

## References and applicability

- [TI SPRABZ0A, section 9](https://www.ti.com/lit/an/sprabz0a/sprabz0a.pdf): staged PWM checks with motor disconnected, followed by unloaded open-loop checks before closing feedback loops. C2000 hardware/current sensing and numeric gains are not copied into this STM32 implementation.
- [SimpleFOC open-loop velocity documentation](https://docs.simplefoc.com/velocity_openloop): advancing a generated angle and bounded excitation without rotor feedback; actual synchronization and heating must be checked. No SimpleFOC library is linked here; we use a fixed 1 kHz timer with timing faults rather than its adaptive scheduling implementation.
- [DFRobot FIT1036 specifications](https://wiki.dfrobot.com/fit1036/): motor pole count and encoder interface; numeric bring-up limits remain our conservative choices.
- [TI BOOSTXL user guide](https://www.ti.com/lit/ug/slvub97/slvub97.pdf) and [DRV8304 datasheet](https://www.ti.com/lit/ds/symlink/drv8304.pdf): EVM connections, hardware PWM mode and driver fault/wake behavior.
