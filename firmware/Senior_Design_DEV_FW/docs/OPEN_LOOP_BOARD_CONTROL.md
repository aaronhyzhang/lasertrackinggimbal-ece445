# Open-loop motor control from board inputs

For the full working-principles explanation, see the [LaTeX guide and separate diagrams](open_loop_control/README.md).

**INLB uses PE10 / CN12 pin 47**, replacing the old PB0 / CN11 pin 34 connection. See [phase-B diagnostics](PHASE_B_PWM_DIAGNOSTIC.md).

Firmware v0.5.0, updated 2026-10-13. The user reports successful motor rotation and that the preceding 100 RPM range looks good. This revision adds bidirectional commands and wait-for-neutral startup; its physical reversal, speed/current/temperature and shutdown timing still need bench validation.

## Configuration and wiring

The existing defaults in Core/Inc/demo_config.h select motor drive:

```c
#define DEMO_PWM_SCOPE_TEST 0
#define DEMO_DEFAULT_OPEN_LOOP 1
#define DEMO_OPEN_BOARD_CONTROL 1
```

Refresh, rebuild and flash Senior_Design_DEV_FW in CubeIDE, with motor-bus power disconnected. Let the MCU run freely during motor operation. The watchdog and nFAULT hardware break remain enabled. Console, network and encoder transactions are not started in this profile.

| Function | Nucleo | Connection / behavior |
| --- | --- | --- |
| Signed speed pot | PA3, A0 / CN9 pin 1; also CN12 pin 37 | 0–3 V, common GND; 1.3–1.7 V is neutral |
| Physical RUN | PG2, CN11 pin 42 | HIGH=3.3 V requests operation; LOW/disconnected stops. Internal pull-down; the user's external 4.7 kOhm pull-down is suitable. Switch/jumper connects to 3.3 V; do not short a driven HIGH directly to GND. |
| Driver ENABLE output | PG4, CN12 pin 69 | EVM J5 pin 9; firmware owns this output. Retain effective ~10 kOhm pull-down at driver. |
| Driver nFAULT input | PE6, CN11 pin 62 | EVM J5 pin 15; real active-LOW fault feedback. Effective pull-up to 3.3 V, about 4.7 kOhm if needed; never a hard tie masking faults. |
| Logic supply / ground | Nucleo 3.3 V / GND | EVM J5 pin 1 VDDIO / J5 pin 4 GND; verify straps/population. |

Six PWM connections: PE9/PE8 -> INHA/INLA, PE11/PE10 -> INHB/INLB, PE13/PE12 -> INHC/INLC. Use hardware MODE=AGND (6-PWM), CAL low, and record GAIN/IDRIVE/VDS settings. Supply motor power terminal J1 from current-limited 24 V; winding leads go to A/B/C at J2. Encoder can remain disconnected. Full header locations are in [BOARD_CONFIG.md](BOARD_CONFIG.md).

Suggested winding record: red=A, black=B, yellow=C, an arbitrary reproducible assignment rather than a verified manufacturer phase/color code. All three are switched winding leads, not supply/ground. Connect or swap only with bus power off and discharged. Physical CW/CCW depends on phase order and viewing side; the high-voltage half retains the existing positive field sequence.

## Analog mapping

| PA3 voltage | Commanded mechanical RPM |
| --- | ---: |
| 0 V | -100 |
| 0.04 V (reported pot minimum) | -96.92 |
| 0.65 V | -50 |
| 1.3–1.7 V inclusive | 0 |
| 2.35 V | +50 |
| 3.0 V | +100 |

Below 1.3 V: RPM = 100 * (V - 1.3) / 1.3. Above 1.7 V: RPM = 100 * (V - 1.7) / 1.3, clamped to +100. Each active span is 1.3 V: sensitivity is 76.923 RPM/V, 2.308 times the preceding 33.333 RPM/V. These are field-speed commands, not measured shaft speeds. Measure actual PA3 voltage; VDDA scaling uses DEMO_ADC_VREF=3.3 V. Inputs 3.0–3.15 V clamp to +100; above 3.15 V, invalid or stale ADC data faults while armed.

**0 V now commands full reverse, not stop.** The previous 100 mV motor deadband is removed. An old PA3-to-GND pull-down no longer gives an unplugged-input stop. If an unplugged command wire should bias neutral, use equal resistors (for example 10 kOhm each) from PA3 to Nucleo 3.3 V and GND, giving approximately 1.65 V; remove any previous pull-down and meter-check pot loading/endpoints. Firmware cannot distinguish an intentional low command from a disconnected signal held low once armed. RUN LOW remains the independent stop.

## Start and wait-for-neutral protection

1. Secure the unloaded motor and check the power-stage waveforms/inhibition using [BENCH_SWITCHING_VALIDATION.md](BENCH_SWITCHING_VALIDATION.md). Start with RUN LOW and the pot around 1.5 V. Connect winding leads only with motor power off.
2. Power/reset the Nucleo; PWM and ENABLE remain off. Keep RUN LOW for at least 20 ms, then HIGH for at least 20 ms. This requests operation.
3. If the pot is outside 1.3–1.7 V, the request waits with PWM/ENABLE off and yellow LD2 blinking at 2 Hz. Bring the pot into the center band and hold it there continuously for 200 ms. A valid/fresh ADC and foreground liveness are required throughout; leaving the band or losing freshness resets the dwell. No second RUN edge is needed for that pending request.
4. Yellow LD2 becomes steady when armed (OPEN_IDLE). Center still produces no PWM/ENABLE. Move slightly above 1.7 V or below 1.3 V to spin: e.g. 1.83 V -> +10 RPM or 1.17 V -> -10 RPM. Allow 10 ms driver wake, 2 s alignment, then a 10 RPM/s speed ramp. Alignment may initially move the rotor in either direction.
5. RUN LOW stops on the next nominal 1 ms control sample and cancels any waiting request. A final GPIO check also gates output authorization. This revokes drive immediately at sampling; it is not a mechanical brake. A new start needs stable RUN LOW then HIGH and a fresh center dwell.

**Boot/reset with RUN already HIGH never arms**, even if the pot later enters neutral. Lower RUN for 20 ms and raise it again to make a fresh request. This boot interlock remains independent of the new analog protection. Software DISABLE/stop also cancels waiting. Faults remain latched: lower RUN, remove the cause, return the pot to center, RESET, then make a fresh RUN transition. A fault release or RUN toggle alone never clears a fault. Red LD3 indicates a latched fault.

## Center stop, restart and reversal

Center commands zero field speed. Running speed ramps toward zero at 10 RPM/s; at zero PWM/ENABLE turn off, followed by a 1 s coast pause and return to OPEN_IDLE. From 100 RPM, the commanded ramp takes approximately 10 s. Neutral is **not energized position holding**; the actual rotor can coast independently.

An opposite-direction request first ramps the existing signed field speed to zero, inhibits all drive for 1 s (OPEN_COAST), then performs a fresh driver wake and 2 s alignment before ramping in the new direction. It never steps directly from positive to negative commutation. If reversal is canceled before reaching zero, it can resume the original direction. Changing direction or entering center during alignment cancels excitation and starts the coast pause. RUN LOW or a fault inhibits during every stage.

The 1 s pause (DEMO_OPEN_COAST_MS) is a provisional bench setting, **not proof of mechanical standstill**. There is no encoder/current feedback here. Begin reversals at low speeds and confirm actual rotor motion/current before trying full speed. Full +100 to -100 command reversal takes roughly 10 s down + 1 s coast + 2 s alignment + 10 s up, plus wake timing. Yellow LD2 stays steady during coast because RUN authorization remains active.

## Limits and validation

Configured 11 pole pairs, 24 V bus, 1 V alignment ramp over 0.5 s within 2 s, 2 V run-vector magnitude, 2 V/s voltage slew, and +/-100 RPM ceiling remain. At either full-scale speed, electrical field frequency magnitude is 18.333 Hz. No phase-current regulation, measured RPM/stall detection or measured standstill is supplied. Supply current limiting can collapse the bus and trigger undervoltage. Check current/temperature and stop for roughness or stall rather than increasing voltage blindly.

Bench-check startup with the pot at both extremes; RUN request must wait with gates off, brief center visits must not arm, and 200 ms in center must arm without spinning. Check RUN LOW cancels waiting, reset with RUN HIGH stays disabled, both low-speed directions work, center decelerates/coasts, and reversal disables/re-aligns. Record scope/current/RPM evidence in [VALIDATION.md](VALIDATION.md).

For MCU-only scope testing set DEMO_PWM_SCOPE_TEST=1: ENABLE stays LOW, identical duties follow the original unipolar 0–3 V mapping, and <=100 mV disables PWM. That diagnostic profile does not use center-neutral protection and must not drive a motor. Return it to 0 afterward. Set DEMO_DEFAULT_OPEN_LOOP=0 and rebuild for the existing encoder/console-controlled position workflow. Set DEMO_OPEN_BOARD_CONTROL=0 for serial-armed open loop; ARM then requires center input but does not use the physical RUN 200 ms waiting workflow.

References: [TI DRV8304 6-PWM/wake/fault behavior](https://www.ti.com/lit/ds/symlink/drv8304.pdf), [ST UM2407 connectors](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf), [DFRobot FIT1036 parameters](https://wiki.dfrobot.com/fit1036/).
