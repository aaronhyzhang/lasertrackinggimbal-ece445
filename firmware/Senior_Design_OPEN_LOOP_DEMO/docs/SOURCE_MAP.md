# Source map and extraction record

Extracted from Senior_Design_DEV_FW v0.5.0, repository source snapshot ee0ee5af944eaa02811046e40f6dc68751c2cce1, on 2026-10-14. The full development project is retained. The standalone project has no runtime mode selector and no dependency on the original folder.

## Presentation reading order

Read demo_config.h -> run_switch.h -> main.c -> demo_app.c -> open_loop.c -> modulation.c -> board.c -> interrupt handlers. The app owns authorization; the motor module owns generated motion; the board owns output inhibition and timer hardware.

## Physical line inventory

These are complete physical lines including comments/blanks; they are not executable statements. Counts exclude vendor system_stm32h7xx.c, startup assembly, Drivers, tests, tools and docs. The HAL configuration is included as an application-owned configuration header. Vendor system initialization is under Drivers/CMSIS, outside the presentation code in Core/Src.

| File | Lines |
| --- | ---: |
| Core/Src/board.c | 220 |
| Core/Src/demo_app.c | 123 |
| Core/Src/main.c | 10 |
| Core/Src/modulation.c | 22 |
| Core/Src/open_loop.c | 116 |
| Core/Src/runtime_support.c | 14 |
| Core/Src/stm32h7xx_hal_msp.c | 3 |
| Core/Src/stm32h7xx_it.c | 17 |
| Core/Inc/board.h | 23 |
| Core/Inc/demo_app.h | 16 |
| Core/Inc/demo_config.h | 24 |
| Core/Inc/open_loop.h | 41 |
| Core/Inc/run_switch.h | 25 |
| Core/Inc/stm32h7xx_hal_conf.h | 37 |
| **Total** | **691** |

## Extraction decisions

- Retained: signed mapping and motor-state progression, neutral startup qualification, speed/voltage ramps, PWM/deadtime/preload settings, driver wake/nFAULT handling, sampled RUN stop, watchdog and timing/freshness interlocks.
- Moved sine modulation/wrap helpers into modulation.c and the open-only data model into open_loop.h.
- Removed position/velocity controllers, feedback unwrap/calibration/ZERO, mode/source/command mailboxes, console/status text, network code/middleware, scope mode and their configuration/types.
- Removed I2C/USART setup, pins and interrupt handlers, extra peripheral driver sources and the unused Ethernet linker section.
- Retained 14 HAL source modules and 28 transitively required vendor headers, identified with the ARM compiler against the reduced HAL configuration. No vendor implementation was rewritten. DMA type headers are needed by ADC/TIM structures; no DMA source/transfer is used.
- Added minimal newlib unsupported-I/O syscall stubs so both toolchains link without default nosys warnings. They provide no command interface.
- Copied and renamed CubeIDE metadata with only Core and Drivers source roots; Tests is a host-only directory.

## Code provenance

open_loop.c and run_switch.h derive from the existing open-loop implementation; board.c derives from the H753 BSP with unused peripheral paths removed. The 1 kHz physical RUN application is reduced to its active path. ST HAL/CMSIS/device startup files retain their original licenses.

A synthetic trace comparison exercised 84,462 application ticks against the original physical profile: state, fault class, authorization, wait flag, LEDs, generated speed/angle/voltage/duties all matched, with zero numeric difference for the scripted inputs. This establishes software behavior for those inputs, not electrical equivalence or plant performance. The local verification harness and CSV traces are in the task workspace tmp/open_loop_extract. See VALIDATION.md for scenarios.
