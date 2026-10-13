# Automatic MCU PWM scope test

**v0.4.1 wiring change:** INLB now comes from **PE10 / CN12 pin 47**, replacing PB0 / CN11 pin 34. Move that wire with power off before energizing the driver. See [phase-B diagnostics](PHASE_B_PWM_DIAGNOSTIC.md).

Firmware v0.3.0, updated 2026-10-13. Software verification does not establish physical waveforms; bench captures are pending.

The user has no command interface on the bench and needs to verify PWM independently of motor startup. Current v0.4.1 normal CubeIDE builds select physical open-loop motor control. Select this diagnostic instead through one line in Core/Inc/demo_config.h:

```c
#define DEMO_PWM_SCOPE_TEST 1
```

1 selects MCU-only automatic ADC-to-duty output. 0 restores motor firmware, where DEMO_DEFAULT_OPEN_LOOP selects speed (1) or position (0). The current motor firmware uses a physical PG2 RUN transition for ARM when DEMO_OPEN_BOARD_CONTROL=1; see OPEN_LOOP_BOARD_CONTROL.md. Scope-test mode still cannot enable the driver.

## Behavior

- No serial commands or encoder are required. The console, Ethernet stack and encoder transactions are not started; basic UART/I2C peripheral setup remains in the shared BSP. No control state machine, wake, alignment, ramp or CALIBRATE/ZERO runs. MCU initialization and the first valid ADC conversion must finish before pulses appear.
- PG4/driver ENABLE is held LOW. board_apply cannot raise it in this build. Do not externally override ENABLE or connect a motor for this test.
- PA3/A0 is the 0–3 V input, referenced to Nucleo GND. At or below 100 mV, ALL six outputs are inactive (not one output held HIGH). The reported approximately 40 mV pot minimum is inside this region.
- Above 100 mV, all three high-side compare values follow V/3, limited to 5–95% for visible pulses. Low-side partners are complementary with nominal 500 ns both-low deadtime. 20 kHz carrier remains fixed; analog changes duty, not frequency or motor speed.
- ADC invalid, older than 20 ms, or above 3.15 V inhibits all outputs. PA3 must still remain within electrical limits. Duty updates nominally every 1 ms.
- PE6/TIM1_BKIN2 must be HIGH to permit PWM. Pull LOW to stop; release HIGH to resume automatically if analog is above the deadband. This automatic resume applies only to the MCU waveform test. Hardware break remains active; break IRQ fault latching is not used for the scope permit input. Other latched board faults inhibit outputs until reset.
- Watchdog remains enabled. Red LD3 and VCP STATUS are not diagnostics in this stripped test path; inspect ADC/CCR/BDTR in CubeIDE with driver disconnected if necessary.

## Wiring and scope procedure

1. Disconnect the EVM/driver and all motor power. Power the Nucleo through ST-LINK USB. Scope ground goes to Nucleo GND.
2. Connect the pot signal to PA3: A0, CN9 pin 1 (also CN12 pin 37). Connect pot ground to Nucleo GND. Confirm actual voltage with a meter.
3. Pull PE6, CN11 pin 62, to 3.3 V through about 4.7 kOhm. The firmware also requests an internal pull-up. A switch/jumper to GND can test inhibition. Never hard-tie to 3.3 V when a real open-drain nFAULT output is connected.
4. Refresh/build/flash normally in CubeIDE, enable Resume and remove the main breakpoint, then let the MCU run. No COM terminal is necessary.
5. Set the pot to about 1.5 V. Scope PE9 relative to GND; expect approximately 0–3.3 V, 20 kHz (50 us period), about 50% duty with deadtime shortening the high interval. Start around 10 us/div; zoom around 100–200 ns/div for paired transitions.
6. Scope PE9 and PE8 together, then the remaining pairs. Within a pair, both cannot be HIGH together; both LOW around an edge is the deadtime. Duty increases on the high output and decreases on its complementary output. All three high outputs match one another in this diagnostic.
7. Test minimum pot: all outputs inactive. Raise it: pulses resume without ARM. Pull PE6 low: stop. Release it high: resume. PG4 must remain LOW throughout. Record actual captures and measured deadtime.

| Phase | High output | Low output |
| --- | --- | --- |
| A | PE9, CN12 pin 52 | PE8, CN12 pin 40 |
| B | PE11, CN12 pin 56 | PE10, CN12 pin 47 |
| C | PE13, CN12 pin 55 | PE12, CN12 pin 49 |

| Analog voltage | Commanded high-output duty before deadtime |
| --- | --- |
| 0–0.10 V | All six inhibited |
| 0.30 V | 10% |
| 0.75 V | 25% |
| 1.50 V | 50% |
| 2.25 V | 75% |
| 3.00 V | 95% clamp |

If there are no pulses at 1.5 V, check PE6 HIGH, scope the exact MCU pin/header, confirm a rebuilt Debug/Senior_Design_DEV_FW.elf was downloaded, and ensure the CPU is running. With the driver disconnected, inspect hadc1/ADC samples, TIM1 CCR1/CCR2/CCR3 (~1250), CR1 CEN, CCER enables and BDTR MOE. A PWM carrier with all GPIOs inactive can result from MOE/break inhibition even though the counter runs. A failed HAL setup can stop in Error_Handler before any ADC conversion.

## Build and return to motor testing

Normal CubeIDE/CLI builds follow the header. CLI --scope-test on/off overrides only this test switch; --scope-test on writes to build/ScopeTest/Debug or Release. Explicit --profile open-loop or closed-loop selects motor firmware unless --scope-test on is also specified. This prevents an explicit motor profile from accidentally producing a diagnostic image.

After MCU captures pass, set DEMO_PWM_SCOPE_TEST to 0 and rebuild/flash before gate-driver or motor testing. Follow BENCH_SWITCHING_VALIDATION.md. A rotating motor field cannot be replaced by three identical duty values; alignment/ramping and motor enable authorization still belong to the motor bring-up path. The physical motor RUN input is now implemented; see OPEN_LOOP_BOARD_CONTROL.md.

References: [ST AN4013, complementary PWM/deadtime and break](https://www.st.com/resource/en/application_note/dm00042534-stm32-crossseries-timer-overview-stmicroelectronics.pdf), [ST UM2407, board connector pins](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf). Nominal timing is based on the implemented 100 MHz TIM1 clock, ARR=2500 and DTG=50, and still needs physical measurement.
