# STM32H743 custom PCB bring-up

**Bring-up status, 2026-10-23:** user reports both driver checks now pass and the motor spins on J3. J4 first-read failure cleared after user resoldering; the precise joint/cause is unconfirmed. New selectable TIM8 support enables the [J4 motor test](docs/J4_MOTOR_TEST.md). The active workspace copy is J4 / MOTOR_RUN / strict fault verification; J4 switching/shutdown remains to be bench-validated. Earlier CPUV and capacitor-tracing history is retained in the procedure.

Self-contained CubeIDE project for the assembled PCB: **selectable J3 / TIM1 or J4 / TIM8 open-loop PWM**, one motor at a time. DFRobot FIT1036, **VR1 / PA4 (POT1)**, **S1 / PD8 ARM**, **S2 / PD9 STOP**. No serial commands, encoder, Ethernet or CAN are needed. The unselected driver stays disabled. The original H753 development/demo projects and H743 generated scaffold are separate projects.

**The shipped default is an I/O check and cannot command PWM or wake a driver.** Choose `PCB_STAGE` and `PCB_TEST_AXIS` in [pcb_config.h](Core/Inc/pcb_config.h), clean/build and flash. Axis 0 is J3; axis 1 is J4. Both axes support every stage; only the selected axis receives PWM:

| Value | Intended check | PWM | Driver ENABLE |
| --- | --- | --- | --- |
| `PCB_IO_CHECK` | First flash: buttons, LEDs and raw pots | Off, GPIO low | Low |
| `PCB_PWM_CHECK` | Six MCU PWM inputs at test points | S1/neutral-qualified open-loop pattern | Low |
| `PCB_DRIVER_CHECK` | Wake selected J3/J4 DRV8304S, verify SPI | All 12 inputs GPIO low | Selected driver only, S1/neutral-qualified; VR1 outside neutral requests wake |
| `PCB_MOTOR_RUN` | Selected unloaded motor after checks | Selected-axis open-loop sine PWM | Selected driver, S1/neutral-qualified |

Read [the bring-up procedure](docs/BRINGUP.md) before first flash. It includes the schematic pin map, STLINK-V3MINI wiring, expected LED/ADC behavior, driver settings and remaining hardware checks.

For the motor stage: release both buttons, center VR1, press S1. Green is steady when armed, blinking while waiting for neutral. Move VR1 out of center to align and ramp. Below center commands one direction, above center the other; either endpoint commands 100 RPM field-equivalent mechanical speed. S2 immediately inhibits PWM and both ENABLEs; use a fresh S1 press at neutral to arm again. A fault lights red and requires MCU reset. No closed-loop speed/position/current regulation is present.

VR1 uses its full nominal 0-3.3 V travel. The old 0-3 V *command* is normalized to ADC travel, retaining its 1.3-1.7 command neutral: **nominal physical neutral is 1.43-1.87 V**, centered at 1.65 V. Verify raw endpoints before driving. VR2 is diagnostic only.

## Build and inspect

In STM32CubeIDE: File > Import > General > Existing Projects into Workspace; select this folder. Build Debug and use its ELF for the H743. This is a manually maintained project without an IOC; do not regenerate it using the unrelated baseline IOC.

From a terminal with Python and the CubeIDE toolchain installed:

```text
python Tools/build.py --config Debug
python Tools/build.py --config Release
python Tools/test.py --cc gcc
```

Host tests also support `--zig path/to/zig.exe`. CLI-only overrides use `--stage io|pwm|driver|motor` and optional `--axis 0|1`; they do not change the CubeIDE header selection. With both overrides, outputs are under `build/STAGE/AXISn/Debug` or `Release`; with only stage, the existing `build/STAGE/Debug` or `Release` paths apply. Default CLI outputs are under `build/Debug` and `build/Release`. CubeIDE outputs are under `Debug` / `Release`.

Watch `g_app_status` in CubeIDE Live Expressions for raw VR1/VR2, command, buttons, ADC age, stage, **test_axis (0=J3, 1=J4)** and motor-model state. DRIVER_CHECK never produces PWM even if the model advances to ALIGN/RUN. `g_driver_status` is the last SPI verification snapshot, not a measurement of live ENABLE. Halt/step only with motors disconnected and drivers inhibited; a breakpoint can leave hardware PWM running. The agent has not flashed hardware; J3 rotation and successful driver checks are user-reported. J4 switching with the new TIM8 code remains pending.

## Source responsibilities

- `demo_app.c`: buttons, neutral dwell, health/authorization, diagnostics and foreground service.
- `open_loop.c`, `modulation.c`: proven signed field-speed ramp, alignment, reversal pause and three-phase sine duty calculation. The wake allowance is extended from 10 to 20 ms for foreground SPI configuration.
- `board.c`, `pcb_pins.h`, `pcb_pwm.h`: H743 clocks, GPIO, ADC, selected TIM1/TIM8 PWM, STOP/fault interrupts, selected-driver SPI routing and watchdog. J3 uses direct TIM1 break; J4 uses PE6 EXTI shutdown plus driver hardware protections. Inhibition clears both clocked timers and both ENABLEs; all PWM pins stay GPIO low in DRIVER_CHECK.
- `drv8304.c`: explicit S-variant register profile, COAST while setting registers, write/readback and periodic verification. No automatic fault clear/retry.
- `pcb_config.h`, `demo_config.h`: selected bring-up stage and inherited motor/ramp limits.

Selected ST HAL/CMSIS support, H743 startup vectors and a 2 MB H743 flash linker script are included. No other workspace project is a build dependency. See [software validation evidence](docs/VALIDATION.md).

## Temporary CPUV-history bench option - 2026-10-23

A motor-disconnected phase-output test may opt into PCB_BENCH_ALLOW_CPUV_HISTORY=1 alongside PCB_MOTOR_RUN. This accepts only recorded CPUV status, leaves the driver protection and live nFAULT interlock active, and preserves the raw fault registers. It does not add a timed diagnostic mode. Restore the option to 0 before motor-connected tests. See [the bench procedure](docs/BRINGUP.md#temporary-cpuv-history-bench-option---2026-10-23) and cpuv_history_accepted in the driver snapshot. Repository defaults remain IO_CHECK / exception=0.

## J4 motor support - 2026-10-23

The active workspace copy now selects J4 / MOTOR_RUN / CPUV-history option=0. Probe ENABLE at TP34/PG7 and MCU PWM at TP23..28. J3 stays disabled. Follow the [J4 motor-test procedure](docs/J4_MOTOR_TEST.md), including the different PE6 interrupt shutdown path and motor-disconnected waveform/STOP/fault checks. Repository/source archive defaults remain J3 / IO_CHECK / history option=0. Earlier driver-only comparison settings are historical.
