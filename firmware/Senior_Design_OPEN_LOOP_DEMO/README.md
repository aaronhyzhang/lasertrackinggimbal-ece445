# Standalone H753 open-loop demo

This is a separate, self-contained STM32CubeIDE project named **Senior_Design_OPEN_LOOP_DEMO** for the current one-motor bidirectional analog demonstration. It contains only the physical RUN-controlled open-loop application and the ST support needed to build it. The original Senior_Design_DEV_FW project remains the development project.

The copies are intentionally independent. Future fixes to shared open-loop behavior must be applied to both projects or the demo must be re-extracted; they will not synchronize automatically.

The demo keeps the v0.5.0 motor behavior: PA3 0 V = -100 RPM, 1.3–1.7 V = neutral, 3 V = +100 RPM. PG2 requests operation; a fresh RUN request waits for 200 ms of continuously valid center input before arming. Reversal ramps to zero, disables for one second, then wakes/aligns before spinning the opposite direction. nFAULT hardware break, sample/liveness/timing checks and watchdog remain.

## Open in CubeIDE

1. Use **File > Import > General > Existing Projects into Workspace**.
2. Select the local Senior_Design_OPEN_LOOP_DEMO folder and import the project. Its name is unique, so it can coexist with Senior_Design_DEV_FW.
3. Select this new project, build Debug or Release, and create/use an ST-LINK SWD debug configuration for **Senior_Design_OPEN_LOOP_DEMO**. Check that the selected ELF belongs to this project.
4. Flash with motor-bus power disconnected, then resume/run the MCU for bench operation. No serial terminal is needed.

The project is hand-owned HAL C; it has no IOC regeneration workflow. There are no links to the original project's source, libraries or build outputs. It can be copied/imported on its own.

## Code to show during the demonstration

| File | What to explain |
| --- | --- |
| Core/Src/main.c | Initialize the board/app, start the control timer/watchdog, run foreground |
| Core/Src/demo_app.c | PG2 RUN, neutral dwell, ADC conversion, control tick, LEDs and liveness |
| Core/Src/open_loop.c | Signed speed map, startup/alignment, ramp, generated angle and reversal |
| Core/Src/modulation.c | Convert generated angle/voltage into three sine PWM duties |
| Core/Inc/demo_config.h | All demo limits, timings and analog thresholds |
| Core/Inc/run_switch.h | LOW/HIGH qualification and immediate sampled LOW stop |
| Core/Src/board.c | GPIO, ADC, timers, complementary PWM/deadtime, break and watchdog |
| Core/Src/stm32h7xx_it.c | Control/ADC/tick and CPU exception entry points |

Core/Inc/open_loop.h has only the open-loop states, inputs, outputs and controller data. No mode selector, position/velocity regulator, calibration/ZERO, encoder implementation, serial protocol, Ethernet stack or scope-test mode is present. I2C and USART peripheral setup and their driver sources were removed.

Drivers/ holds the required ST HAL/CMSIS dependencies and their licenses. Only the compiler-identified header dependency set and selected HAL modules are included. Generic ST files still contain their module's normal APIs; they are vendor support rather than additional application features. ADC/TIM structures require DMA type headers even though this demo performs no DMA transfers. The standard MCU startup vector table has weak names for the device's other interrupts, not implementations of unused features.

runtime_support.c supplies the minimal newlib syscall ABI; stream I/O is unavailable. The vendor system_stm32h7xx.c is under Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates; startup assembly remains under Core/Startup. This keeps Core/Src limited to the demo and its board/runtime support. Tests and Tools are host/build utilities and are not compiled into the MCU image. [Code inventory and extraction notes](docs/SOURCE_MAP.md).

## Wiring and operation

| Function | Nucleo | BOOSTXL-DRV8304H |
| --- | --- | --- |
| A high / low | PE9 / PE8 | J4 pins 1 / 3 |
| B high / low | PE11 / PE10 | J4 pins 5 / 7 |
| C high / low | PE13 / PE12 | J4 pins 9 / 11 |
| ENABLE | PG4, CN12 pin 69 | J5 pin 9 |
| nFAULT | PE6, CN11 pin 62 | J5 pin 15, effective pull-up to 3.3 V |
| Logic power / GND | 3.3 V / common GND | J5 pin 1 / J5 pin 4; verify rail straps |
| Pot | PA3, A0/CN9 pin 1 | Separate 0–3 V command referenced to common GND |
| RUN | PG2, CN11 pin 42 | Switch/jumper to 3.3 V; LOW/disconnected stops |

B-low remains **PE10/CN12 pin 47**, not PB0. Use DRV8304H hardware MODE=AGND/6-PWM, CAL low, and the same verified protection/drive straps. Retain driver ENABLE pull-down, nFAULT pull-up and an appropriate RUN pull-down (the existing 4.7 kOhm is suitable). Supply the motor bus from current-limited 24 V. The three winding leads go to A/B/C, never directly to a power rail.

Start RUN LOW, pot near 1.5 V. After at least 20 ms LOW, raise RUN for at least 20 ms. An out-of-center request waits, yellow LD2 blinking; hold center 200 ms to arm, yellow steady. Leave center gently to start: 1.83 V is about +10 RPM, 1.17 V about -10 RPM. Driver wake is 10 ms; alignment is 1 V (0.5 s ramp, 2 s total), followed by a 10 RPM/s speed ramp toward the signed command and 2 V running magnitude. PWM remains 20 kHz with nominal 500 ns deadtime.

Boot/reset with RUN already HIGH never arms until a fresh LOW-to-HIGH request. RUN LOW cancels pending startup and inhibits on the next nominal 1 ms sample, with a final GPIO check before applying outputs. Center ramps to zero and coasts; it does not hold position. Red LD3 means a latched fault: fix the cause, RUN LOW, RESET, fresh RUN/center sequence.

**0 V is reverse.** Do not use the former PA3 ground pull-down as a disconnected-input stop. If desired, equal 10 kOhm resistors from PA3 to 3.3 V/GND bias an unplugged lead near 1.65 V; check pot loading/endpoints. Firmware cannot detect an unplugged wire held low after arming.

The one-second reversal pause is not measured standstill. This demo has no phase-current regulation, measured RPM or stall detection. Validate the extracted image at low speed before the demonstration. MCU/driver/loaded gate checks still matter; no source extraction or build flashes or physically validates hardware.

## Build and tests

From this directory:

    python Tools/build.py --config Debug
    python Tools/build.py --config Release
    python Tools/test.py --cc clang

Or pass --zig <path-to-zig.exe> for host tests. The build helper discovers ARM GCC from CubeIDE under C:/ST; --toolchain <bin-directory> overrides it. No other firmware project is needed. Native CubeIDE outputs are Debug/ and Release/; CLI outputs are build/Debug and build/Release. All images are named Senior_Design_OPEN_LOOP_DEMO.

[Verification](docs/VALIDATION.md) records controller/app tests, the original-versus-extracted comparison, standalone/native builds and remaining bench checks. The earlier detailed motor-physics LaTeX guide is retained in the development project docs/open_loop_control; its state/physics explanation applies, but its mixed-project file names and code counts do not describe this extraction.
