# J4 open-loop motor test

**Current bench-image note, 2026-10-24:** all temporary settings are reset to baseline after the user-reported copper-short repair. Fault capture is off. Complete [the motor-disconnected post-repair output check](POST_REPAIR_OUTPUT_CHECK.md) first; post-repair switching is still pending.

2026-10-23. User reports both driver checks now pass and an unloaded motor spins on J3. Before that, J4 returned `0x7ff` on its first register-2 readback; the other snapshot entries were unread, not fault-free status. The user reports that J4 passed after desoldering/resoldering. This suggests a connection issue but does not identify a particular failed joint. J4 motor operation with the new TIM8 implementation remains untested on hardware.

## Select one motor

The existing `Senior_Design_PCB_BRINGUP` project now supports either connector in all four bring-up stages. In `Core/Inc/pcb_config.h`, the active workspace copy is:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 1
#define PCB_PWM_SYNC_START 0
#define PCB_HS_SOURCE_MA 15
#define PCB_VDS_THRESHOLD_MV 150
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 0
```

Axis 0 selects J3/TIM1; axis 1 selects J4/TIM8. Change only `PCB_TEST_AXIS` to switch motors after each connector's checks. This is still a **single-motor** application: the other ENABLE and its six PWM pins stay low. Repository/archive defaults remain J3 / IO_CHECK / strict verification. The CPUV-history exception is off for motor testing.

VR1 remains the command for either axis; VR2 is not used. S1 ARM, neutral qualification, S2 STOP, alignment, bidirectional ramp and reversal pause are unchanged. Endpoints command +/-100 RPM field-equivalent mechanical speed, with 10 RPM/s acceleration. Actual rotor speed is not measured. See [the main procedure](BRINGUP.md#stage-4-motor-run) for the pot mapping and motor limits.

## J4 connections and probe points

| Signal | MCU pin | Peripheral / test point |
| --- | --- | --- |
| Phase A high / low PWM input | PC6 / PA5 | TIM8_CH1 / CH1N, AF3; TP23 / TP24 |
| Phase B high / low PWM input | PC7 / PB14 | TIM8_CH2 / CH2N, AF3; TP25 / TP26 |
| Phase C high / low PWM input | PC8 / PB15 | TIM8_CH3 / CH3N, AF3; TP27 / TP28 |
| ENABLE / CAL | PG7 / PG6 | TP34 / TP33; CAL stays low |
| nCS | PA9 | Selected SPI device; J3 PE5 stays high |
| Enable / fault indicators | PC10 / PC11 | TP36 / TP35; software indicators |
| Shared nFAULT | PE6 | Active low, EXTI line 6 for J4 |
| Motor phases | J4 pins 1 / 2 / 3 | A / B / C |

These GPIO bindings follow the reviewed schematic pages 3/5/7. [ST DS12110 Table 10](https://www.st.com/resource/en/datasheet/stm32h743ai.pdf) confirms AF3 for all six TIM8 outputs. Both selected timer variants use the same 100 MHz kernel, center-aligned 20 kHz carrier, three complementary pairs and nominal 500 ns MCU dead time. Three duty references have 120 electrical-degree spacing; the PWM carrier pulses themselves are not delayed by 120 degrees.

## Bench sequence

1. Power down before moving wires; keep the J3 motor disconnected while testing J4. Save/build/flash Debug and Resume. Confirm `g_app_status.test_axis=1` and `stage=3`.
2. For the first J4 waveform check, keep its motor disconnected too. Optionally select `PCB_PWM_CHECK` first to scope TP23..28 with both ENABLEs low; restore `PCB_MOTOR_RUN` for power-stage switching. Center VR1 (nominal 1.43..1.87 V), release both buttons, press/release S1, allow 200 ms neutral qualification, then move slightly outside neutral.
3. Check the six MCU inputs for clean complementary switching and dead time. In MOTOR_RUN, PG7/TP34 should go high and PG4/TP30 remain low. Verify `verified=true`, `read_mask=0x7f`, register 0/1 both zero, `driver_good=true`, and no motor fault after startup. The state model progresses through wake, alignment and ramp.
4. Scope J4 phase outputs relative to board GND. Expect bus-amplitude carrier pulses with changing duty. All grounded scope clips go to board GND, never a phase or high-side source. Measure high-side VGS only using a properly rated differential probe or suitable two-channel subtraction with both grounds at board GND.
5. With the motor still disconnected, press S2 during PWM: both ENABLEs must go low and PWM stop. Check J4 nFAULT shutdown as well: after reset/arming and active PWM, pull the **verified PE6 nFAULT net** low through 1 kOhm to board GND. This is an open-drain signal with a pull-up; verify it actually reaches a logic-low voltage. Expect a latched `FAULT_DRIVER`, both ENABLEs low and PWM stopped. Remove the resistor; reset is required to clear the software fault. Capture shutdown timing on the scope; software tests do not establish its latency.
6. Power down. Connect the unloaded FIT1036 phase leads to J4 A/B/C, retaining the same wire order if you want comparable direction. Use the proven 24 V supply setup and its proposed 2 A bus limit, center VR1 and repeat S1/neutral/start. Expect approximately 2 s alignment followed by a speed ramp. Test small commands in both directions and S2 before increasing speed. Monitor supply/phase current and device temperature. A bus current limit does not regulate winding current.

## Fault shutdown differs between axes

J3 retains PE6 AF1 -> TIM1_BKIN2 hardware PWM shutdown and its break ISR. PE6 has **no TIM8 break alternate function**, so this PCB does not provide the same direct hardware break for J4. For J4, PE6 uses a falling-edge interrupt at priority 0, shared with S2's EXTI9_5 vector; control TIM7 is priority 2. While PWM is authorized, even a brief nFAULT edge latches a fault, clears MOE on both clocked timers and lowers both ENABLEs. STOP, MCU exceptions, stale-data/control failures and the watchdog also retain their existing inhibition paths. No automatic restart is added.

The expected nFAULT wake pulse is ignored by this interrupt before PWM authorization. During wake, strict SPI verification and the live nFAULT level must pass within the existing 20 ms allowance. Immediately before TIM8 MOE is set, old wake edges are cleared and the live nFAULT level is checked again. A subsequent edge remains pending for the ISR. The DRV8304's own enabled fault protections act within the driver; the MCU interrupt is additional shutdown, **not a hardware break or a measured response-time guarantee**. See [TI SLVSE39B sections 7.3.5 and 7.4](https://www.ti.com/lit/ds/symlink/drv8304.pdf) and [ST AN4277](https://www.st.com/content/st_com/en/technical-documents/an4277-how-to-use-pwm-shutdown-for-motor-control-and-digital-power-conversion-on-stm32-mcus-stmicroelectronics.html).

For a future direct TIM8 break, route the shared fault signal to a verified TIM8_BKIN/BKIN2-capable pin and validate that wiring and firmware separately. No jumper/rework is required for this documented interrupt-based bench test. This build does not claim dual-motor operation, closed-loop control, new current regulation or validated production fault latency.

The working J3 sources before this change are backed up under the workspace's `Senior_Design_FW/tmp/pcb_bringup/j3_spin_working_backup`. The agent has built/tested the software but has not flashed or tested J4 on hardware.
