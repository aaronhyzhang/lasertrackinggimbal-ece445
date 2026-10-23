# STM32H743 custom PCB bring-up

**Bring-up status, 2026-10-23:** the driver reports CPUV. User measured >100 MOhm between VCP and VM and confirmed the 1 uF storage / 22 nF flying capacitors are fitted. C44 is correctly drawn between VCP and VM; the earlier short/missing-capacitor diagnosis and full-bus rating warning were tracing errors and are withdrawn. Keep motors disconnected for the [ENABLE/VCP startup capture](docs/BRINGUP.md#next-measurement-charge-pump-startup); no trace cut or added capacitor is indicated.

Self-contained CubeIDE project for the assembled PCB, **J3 / axis 0**, DFRobot FIT1036, **VR1 / PA4 (POT1)**, **S1 / PD8 ARM**, **S2 / PD9 STOP**. No serial commands, encoder, Ethernet or CAN are needed. J4 stays disabled. The original H753 development/demo projects and H743 generated scaffold are separate projects.

**The shipped default is an I/O check and cannot command PWM or wake a driver.** After each bench check, change the one `PCB_STAGE` line in [pcb_config.h](Core/Inc/pcb_config.h), clean/build and flash:

| Value | Intended check | J3 PWM | J3 ENABLE |
| --- | --- | --- | --- |
| `PCB_IO_CHECK` | First flash: buttons, LEDs and raw pots | Off, GPIO low | Low |
| `PCB_PWM_CHECK` | Six MCU PWM inputs at test points | S1/neutral-qualified open-loop pattern | Low |
| `PCB_DRIVER_CHECK` | Wake DRV8304S, configure/read back SPI | Always off | S1/neutral-qualified; nonzero VR1 requests wake |
| `PCB_MOTOR_RUN` | Unloaded motor spin after the above checks | Open-loop sinusoidal commutation | S1/neutral-qualified |

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

Host tests also support `--zig path/to/zig.exe`. CLI-only stage overrides use `--stage io|pwm|driver|motor` and put outputs under `build/STAGE/Debug` or `build/STAGE/Release`; they do not change the CubeIDE header selection. Default CLI outputs are under `build/Debug` and `build/Release`. CubeIDE outputs are under `Debug` / `Release`.

Watch `g_app_status` in CubeIDE Live Expressions for raw VR1/VR2, command, buttons, ADC age and motor state. `g_driver_status` is the last SPI verification snapshot, not a measurement of live ENABLE. Halt/step only with motors disconnected and drivers inhibited; a breakpoint can leave hardware PWM running. No firmware has been flashed by the agent and no custom-PCB switching result is claimed.

## Source responsibilities

- `demo_app.c`: buttons, neutral dwell, health/authorization, diagnostics and foreground service.
- `open_loop.c`, `modulation.c`: proven signed field-speed ramp, alignment, reversal pause and three-phase sine duty calculation. The wake allowance is extended from 10 to 20 ms for foreground SPI configuration.
- `board.c`, `pcb_pins.h`: H743 clocks, GPIO, ADC, TIM1/dead time/break, STOP interrupt, SPI transport and watchdog. Both drivers are inhibited before clock/peripheral setup; J4 PWM remains GPIO low.
- `drv8304.c`: explicit S-variant register profile, COAST while setting registers, write/readback and periodic verification. No automatic fault clear/retry.
- `pcb_config.h`, `demo_config.h`: selected bring-up stage and inherited motor/ramp limits.

Selected ST HAL/CMSIS support, H743 startup vectors and a 2 MB H743 flash linker script are included. No other workspace project is a build dependency. See [software validation evidence](docs/VALIDATION.md).
