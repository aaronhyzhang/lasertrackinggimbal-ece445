# J4 GPIO, PWM and fault-path audit

**Historical diagnostic procedure:** active-image selections below are superseded by [the post-repair baseline](POST_REPAIR_OUTPUT_CHECK.md); temporary experiment flags are now off.

**Follow-up:** the active bench image now adds [bounded post-trip SPI readback](FAULT_READBACK.md). The passive trip record remains, but capture=1 intentionally retains the already-awake selected ENABLE briefly with PWM off. The earlier immediate ENABLE-low policy below describes capture=0.

2026-10-23. The user requested a firmware recheck because J4 passed the earlier driver-only test after resoldering, but MOTOR_RUN now trips with no motor connected. User reports all six MCU PWM outputs look okay in PWM_CHECK. The first saved MOTOR_RUN snapshot had verified=true, registers `[0,0,0x080,0x300,0x300,0x300,0x280]`, motor fault=FAULT_DRIVER, a board-layer driver-fault latch and approximately 22 ms from wake to the app's fault timestamp.

**Result: no pin-map or deterministic GPIO/EXTI configuration defect was found. No physical cause has been established, and a firmware/timing problem remains possible.** The driver-only pass did not exercise switching: its physical PWM inputs stayed low. A valid SPI read before switching does not diagnose a later fault.

## Bindings checked

| J4 function | Binding verified against schematic and source |
| --- | --- |
| A high / low | PC6 / PA5, AF3 TIM8_CH1 / CH1N, TP23 / TP24 |
| B high / low | PC7 / PB14, AF3 TIM8_CH2 / CH2N, TP25 / TP26 |
| C high / low | PC8 / PB15, AF3 TIM8_CH3 / CH3N, TP27 / TP28 |
| ENABLE / CAL | PG7 / PG6, TP34 / TP33; CAL low |
| Chip select | PA9; J3 PE5 stays high |
| Shared nFAULT | PE6 input, falling-edge EXTI6; EXTI6 mapped to port E |
| Command | VR1 / PA4 / ADC1_INP18; VR2 does not command motion |
| ARM / STOP | PD8 / PD9, pressed low; STOP uses EXTI9 |

Reviewed schematic: physical pages 3, 5 and 7 of the existing nine-page PDF; SHA-256 `2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144`. [ST DS12110 Table 10](https://www.st.com/resource/en/datasheet/stm32h743ai.pdf) confirms the AF3 TIM8 assignments and that PE6 has TIM1_BKIN2, not a TIM8 break AF. [TI SLVSE39B](https://www.ti.com/lit/ds/symlink/drv8304.pdf) establishes the S-variant SPI/pin definitions and fault reporting.

`board.c` owns initialization; the main/MSP/interrupt files do not subsequently run generated GPIO/peripheral initialization that overwrites these bindings. SPI2 stays on PB10/PC2_C/PC3_C, leaving PB14/PB15 assigned to TIM8. PE6 is never configured as a GPIO output in the J4 path. The vendor GPIO HAL configures PE6 as an input, selects port E for EXTI6 and enables only falling-edge detection. This happens after the PC6 PWM pin is configured; the shared pin number does not map EXTI6 to PC6.

The selected timer retains the inherited center-aligned 20 kHz carrier, 500 ns nominal dead time, complementary polarity, preloaded duty updates and idle-low settings. Only the selected six pins use timer AF; J3 PWM pins stay GPIO-low during a J4 test. Both ENABLEs are claimed low before peripheral setup. PWM remains inhibited while the driver wakes/configures, and requires ready plus a high live nFAULT before the first output authorization. No pin mode, timer parameter, driver profile, current setting, protection threshold, wake delay or retry policy was changed by this audit. One obsolete axis-1-only-driver-check comment in demo_app.h was corrected.

## Software checks

The existing 18 host executables and four invalid-config compilation checks pass. A new Windows x64 audit executes the **actual board.c and vendor GPIO HAL** on privately mapped, simulated register memory, with assertions explicitly enabled even under optimization:

```text
python Tools/test_bsp.py --zig path/to/zig.exe
```

It verifies all six J4 pin modes/AF values, the inactive J3 pins, ENABLE/CS/CAL output modes, PE6 input mode and port-E EXTI routing, falling-only trigger, first output authorization, ignored wake edges, stale pending-bit clearance, an empty/stale NVIC entry, live-low rejection, active fault inhibition, a brief fault pulse already released before sampling, first-trip preservation and STOP priority. Unrelated peripheral services abort if reached; the test does not silently accept their fake success.

This audit simulates CPU/NVIC behavior and EXTI write-one-to-clear semantics. It does not emulate real timer counting, ADC/SPI transactions, analog levels, concurrent interrupt timing or HAL timer initialization. Native CubeIDE J4 MOTOR_RUN Debug/Release and CLI J3 MOTOR_RUN Debug builds pass without warnings. Electrical continuity, gate switching, real EXTI behavior and shutdown latency still need scope validation. A passing simulation is not proof that the assembled board or every firmware path is correct.

## New passive first-trip record

The existing immediate shutdown now records a few state/register values immediately before inhibition and publishes `g_driver_trip` after PWM/ENABLE are off. There is no SPI transfer, wait, longer observation window, retry or fault bypass. The record is frozen until MCU reset, so subsequent shutdown state cannot obscure the first source.

| g_driver_trip field | Meaning |
| --- | --- |
| `source` | Which board-layer driver check tripped; see below |
| `wake_age_ms` | HAL milliseconds from ENABLE request to this board trip; may precede the app's later fault timestamp |
| `nfault_high` | PE6 sampled before inhibition: false means low at that sample |
| `driver_ready` | Configuration/readiness flag before inhibition |
| `pwm_authorized` | Whether PWM had been authorized before the trip |

| source | Interpretation |
| --- | --- |
| `DRIVER_TRIP_NONE` (0) | No board-layer driver trip has been recorded; not a clean bill of health |
| `DRIVER_TRIP_PWM_PERMISSION` (1) | PWM was requested but ready or live nFAULT failed |
| `DRIVER_TRIP_START_NFAULT_LOW` (2) | The final live-level check before first authorization saw nFAULT low |
| `DRIVER_TRIP_NFAULT_EDGE` (3) | J4 EXTI6 had a pending falling edge while PWM was authorized |
| `DRIVER_TRIP_TIM1_BREAK` (4) | J3 TIM1 break interrupt |

A sampled high nFAULT after an edge does not establish a false interrupt: a real narrow pulse can release before the ISR samples it. The shared line can also be asserted by the other driver. The record identifies a software path, not a decoded hardware fault. `g_driver_status` remains the previous SPI snapshot and is not refreshed after shutdown; SPI is disabled in driver sleep.

The active workspace is prepared as **J4 / MOTOR_RUN / CPUV-history option=0**. Its prior Core and PWM_CHECK configuration are backed up at `Senior_Design_FW/tmp/pcb_bringup/j4_before_trip_snapshot`. Rebuild/flash Debug, Resume and reproduce with both motors disconnected. Record all five `g_driver_trip` fields alongside `g_driver_status`. Capture TP34 ENABLE and PE6 nFAULT, triggering on ENABLE falling with pretrigger; distinguish nFAULT before shutdown from the normal sleep pulse after ENABLE falls. No new hardware attempt or flash has been performed by the agent.
