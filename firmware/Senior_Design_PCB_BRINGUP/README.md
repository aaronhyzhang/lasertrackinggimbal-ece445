# STM32H743 custom PCB bring-up

**Working checkpoint, 2026-10-24:** user reports J4 motor rotation after repairing the phase-B high-side gate-to-GND copper short. Current defaults are MOTOR_RUN / J4, +/-150 RPM field-speed command, 2.5 V phase amplitude and 10 RPM/s acceleration, with baseline protections. See [the saved checkpoint](docs/WORKING_CHECKPOINT_2026-10-24.md).

Self-contained CubeIDE project for the assembled PCB: **selectable J3 / TIM1 or J4 / TIM8 open-loop PWM**, one motor at a time. DFRobot FIT1036, **VR1 / PA4 (POT1)**, **S1 / PD8 ARM**, **S2 / PD9 STOP**. No serial commands, encoder, Ethernet or CAN are needed. The unselected driver stays disabled. The original H753 development/demo projects and H743 generated scaffold are separate projects.

**This checkpoint defaults to MOTOR_RUN / J4 and requires S1/neutral qualification to arm.** For a first-flash I/O-only check, select `PCB_IO_CHECK`. Choose `PCB_STAGE` and `PCB_TEST_AXIS` in [pcb_config.h](Core/Inc/pcb_config.h), clean/build and flash. Axis 0 is J3; axis 1 is J4. Both axes support every stage; only the selected axis receives PWM:

| Value | Intended check | PWM | Driver ENABLE |
| --- | --- | --- | --- |
| `PCB_IO_CHECK` | First flash: buttons, LEDs and raw pots | Off, GPIO low | Low |
| `PCB_PWM_CHECK` | Six MCU PWM inputs at test points | S1/neutral-qualified open-loop pattern | Low |
| `PCB_DRIVER_CHECK` | Wake selected J3/J4 DRV8304S, verify SPI | All 12 inputs GPIO low | Selected driver only, S1/neutral-qualified; VR1 outside neutral requests wake |
| `PCB_MOTOR_RUN` | Selected unloaded motor after checks | Selected-axis open-loop sine PWM | Selected driver, S1/neutral-qualified |

**Post-repair baseline:** active workspace J4 / MOTOR_RUN, high-side source=15 mA, VDS=150 mV, original startup, strict CPUV verification and immediate shutdown (fault capture off). The user has since reported motor rotation; the saved tuning is documented in the checkpoint. Only required timer/GPIO/fault-route differences remain between J3 and J4.

Read [the bring-up procedure](docs/BRINGUP.md) before first flash. It includes the schematic pin map, STLINK-V3MINI wiring, expected LED/ADC behavior, driver settings and remaining hardware checks.

For the motor stage: release both buttons, center VR1, press S1. Green is steady when armed, blinking while waiting for neutral. Move VR1 out of center to align and ramp. Below center commands one direction, above center the other; either endpoint commands 150 RPM field-equivalent mechanical speed. S2 immediately inhibits PWM and both ENABLEs; use a fresh S1 press at neutral to arm again. A fault lights red and requires MCU reset. No closed-loop speed/position/current regulation is present.

VR1 uses its full nominal 0-3.3 V travel. The old 0-3 V *command* is normalized to ADC travel, retaining its 1.3-1.7 command neutral: **nominal physical neutral is 1.43-1.87 V**, centered at 1.65 V. Verify raw endpoints before driving. VR2 is diagnostic only.

## Build and inspect

In STM32CubeIDE: File > Import > General > Existing Projects into Workspace; select this folder. Build Debug and use its ELF for the H743. This is a manually maintained project without an IOC; do not regenerate it using the unrelated baseline IOC.

From a terminal with Python and the CubeIDE toolchain installed:

```text
python Tools/build.py --config Debug
python Tools/build.py --config Release
python Tools/test.py --cc gcc
```

Host tests also support `--zig path/to/zig.exe`. The optional Windows BSP audit uses `python Tools/test_bsp.py --zig path/to/zig.exe --capture 0` and `--capture 1` to check both shutdown policies. CLI-only overrides use `--stage io|pwm|driver|motor` and optional `--axis 0|1`; they do not change the CubeIDE header selection. With both overrides, outputs are under `build/STAGE/AXISn/Debug` or `Release`; with only stage, the existing `build/STAGE/Debug` or `Release` paths apply. Default CLI outputs are under `build/Debug` and `build/Release`. The capture option is default-off and restricted to MOTOR_RUN; select capture=0 for normal motor operation and all other stages. CubeIDE outputs are under `Debug` / `Release`.

Watch `g_app_status` in CubeIDE Live Expressions for raw VR1/VR2, command, buttons, ADC age, stage, **test_axis (0=J3, 1=J4)** and motor-model state. DRIVER_CHECK never produces PWM even if the model advances to ALIGN/RUN. `g_driver_status` is the last SPI verification snapshot, not a measurement of live ENABLE. Halt/step only with motors disconnected and drivers inhibited; a breakpoint can leave hardware PWM running. The agent has not flashed hardware; J3 rotation and successful driver checks are user-reported. J4 post-repair rotation is also user-reported; maximum-command stability has not been measured by the agent.

## Source responsibilities

- `demo_app.c`: buttons, neutral dwell, health/authorization, diagnostics and foreground service.
- `open_loop.c`, `modulation.c`: proven signed field-speed ramp, alignment, reversal pause and three-phase sine duty calculation. The wake allowance is extended from 10 to 20 ms for foreground SPI configuration.
- `board.c`, `pcb_pins.h`, `pcb_pwm.h`: H743 clocks, GPIO, ADC, selected TIM1/TIM8 PWM, STOP/fault interrupts, selected-driver SPI routing and watchdog. J3 uses direct TIM1 break; J4 uses PE6 EXTI shutdown plus driver hardware protections. Inhibition clears both clocked timers and both ENABLEs; all PWM pins stay GPIO low in DRIVER_CHECK.
- `drv8304.c`: explicit S-variant register profile, COAST while setting registers, write/readback and periodic verification. No automatic fault clear/retry.
- `pcb_config.h`, `demo_config.h`: selected bring-up stage and inherited motor/ramp limits.

Selected ST HAL/CMSIS support, H743 startup vectors and a 2 MB H743 flash linker script are included. No other workspace project is a build dependency. See [software validation evidence](docs/VALIDATION.md).

## Diagnostic history

The [firmware comparison](docs/J3_J4_FIRMWARE_COMPARISON.md), [fault readback](docs/FAULT_READBACK.md), [synchronized startup](docs/PWM_SYNC_START_TEST.md), [gate-current/SPI audit](docs/HS_GATE_DRIVE_TEST.md) and [VDS threshold test](docs/VDS_THRESHOLD_TEST.md) record earlier experiments. Their active-image settings are superseded by [the post-repair baseline](docs/POST_REPAIR_OUTPUT_CHECK.md). Diagnostic options remain available but are off in the restored baseline. The current working checkpoint supersedes those historical defaults: repository and active CubeIDE headers both select MOTOR_RUN / J4.
