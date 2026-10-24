# J4 output check after gate-net repair

> Later status: the user reports motor rotation after the copper repair. Current saved defaults and tuning are in [WORKING_CHECKPOINT_2026-10-24.md](WORKING_CHECKPOINT_2026-10-24.md). This document retains the earlier output-check procedure.

2026-10-24. The user reports finding and repairing an outer-layer copper connection shorting the J4 phase-B high-side gate net to GND. The short persisted with both its resistor and MOSFET removed, supporting a PCB copper connection rather than the removed parts. The post-repair electrical and switching results are still pending; this document records the user's repair report, not an agent-verified hardware pass.

This procedure supersedes the active-image instructions in the earlier PWM-sync, 45 mA source-current, VDS-threshold and fault-readback experiments. Those documents remain diagnostic history.

## Restored common configuration

The existing Senior_Design_PCB_BRINGUP project is prepared for J4 phase-output testing with no motor:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 1
#define PCB_PWM_SYNC_START 0
#define PCB_HS_SOURCE_MA 15
#define PCB_VDS_THRESHOLD_MV 150
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 0
```

The high-side drive is back to 15 mA source / 30 mA sink; low-side drive remains 15/30 mA. VDS protection is back to 0.15 V with latched all-bridge shutdown, 400 ns driver dead time, 4 us TDRIVE, CSA gain=20 and unchanged shunt-sense protection. The synchronized-start experiment is off: both axes again use the original counter/output-release sequence. Fault-history acceptance is off. Temporary retention of ENABLE for post-trip SPI reads is off: normal inhibition clears PWM and lowers both ENABLEs immediately in the firmware shutdown path, without the diagnostic readback window. Hardware/interrupt latency is not measured by this wording.

All command/ramp settings are shared: VR1, S1/S2, boot/neutral guard, 20 kHz center-aligned carrier, nominal 500 ns MCU dead time, alignment, signed speed ramp and reversal pause. No cross-wiring test was implemented; no PWM jumper wires are required. Restore ordinary board connections if any temporary wiring was fitted independently.

Switching to J3 now requires changing only PCB_TEST_AXIS to 0. The remaining necessary axis differences are timer/pin selection, ENABLE/CAL/CS/indicator pins, and the shared nFAULT route: J3 uses TIM1_BKIN2 hardware break, J4 uses PE6 falling-edge EXTI to inhibit TIM8, alongside the DRV8304's own protection. J4 cannot use J3's TIM1 pin assignments or direct hardware break without PCB rework. Both use the same shared SPI2 bus/settings and driver register profile.

## Before powering the repaired bridge

Keep both motors disconnected. Confirm the repaired isolated gate copper no longer has the hard short to GND. Inspect/clean the cut for conductive debris and unintended damage. Reinstall the correct MOSFET and series gate resistor if still removed; do not power with a floating gate. Check gate-path continuity and no new gate/source/drain bridges. Check that the isolation repair preserves the intended source/drain power connections. Use the established current-limited 24 V bench supply.

## Flash and observe J4

1. Refresh the existing project in CubeIDE if needed, clean/build Debug, flash Debug/Senior_Design_PCB_BRINGUP.elf and Resume. Neither a new project nor a console is needed.
2. Confirm g_app_status.test_axis=1 and stage=3. Drivers should start disabled with no automatic motion. Release S1/S2 and center VR1 in the nominal physical neutral band 1.43..1.87 V, centered about 1.65 V.
3. Press/release S1 and let neutral qualify. Move VR1 slightly outside neutral. J4 ENABLE is TP34/PG7; J3 ENABLE TP30/PG4 stays low. Green indicates arming; verify the live driver/status fields rather than relying on LED color alone.
4. Check g_driver_status.verified=true and g_app_status.driver_good=true after wake. Expected register profile:

   | Address | Decimal | Hex |
   | --- | --- | --- |
   | 0 | 0 | 0x000 |
   | 1 | 0 | 0x000 |
   | 2 | 128 | 0x080 |
   | 3 | 768 | 0x300 |
   | 4 | 768 | 0x300 |
   | 5 | 768 | 0x300 |
   | 6 | 640 | 0x280 |

   MOTOR_OPEN_ALIGN then MOTOR_OPEN_RUNNING with FAULT_NONE means the state model progressed without a detected fault, not that rotor speed was measured. With capture disabled, g_driver_fault.state=DRIVER_FAULT_DISABLED is expected; old post-trip capture fields do not provide fresh register data. A no-trip g_driver_trip structure holds defaults, not live pin readings.

5. Scope J4 phase A/B/C at the connector relative to board GND. All grounded probe clips go to board GND, never a phase or high-side source. Expect approximately bus-amplitude 20 kHz switching when drive is active. Near startup the duty is near 50%, then the alignment/running envelope develops. The modulation references are 120 electrical degrees apart; the PWM carrier pulses are not three fixed square waves spaced by 120 degrees. Use about 10 us/div to inspect carrier pulses. Pay particular attention to repaired phase B now responding to its input command.
6. J4 MCU inputs remain TP23/24 (A high/low), TP25/26 (B high/low), TP27/28 (C high/low), with complementary switching and nominal dead time. Keep the same probe assignments while comparing input to phase output if needed.
7. Press S2 while outputs switch. Confirm PWM stops and both ENABLEs go low. A fault is latched until MCU reset; STOP requires a fresh neutral-qualified S1 arm. If a fault occurs, stop the test and report app fault/trip source and observed waveforms; the current normal build does not retain the post-trip SPI window.

## Before connecting a motor

First establish all three output waveforms, clean fault status and physical S2 shutdown after the repair. Then power off before connecting one unloaded motor; do not connect it merely because a green LED is on. Motor-connected testing uses the same baseline settings and normal capture-off shutdown. The current firmware does not regulate winding current; the supply's bus-current limit is not a 4 A phase-current loop.

## Software checks and configuration provenance

Repository/source-archive defaults remain IO_CHECK/axis=0, with all diagnostic options off and baseline drive/protection values. The active CubeIDE header differs only in stage=MOTOR_RUN and axis=1. The prior active experimental configuration is saved as Senior_Design_FW/tmp/pcb_bringup/before_postrepair_config.h. Core source logic, SPI timing, pin definitions and motor-control parameters are unchanged by this cleanup; only default/active configuration selections and documentation were changed. No agent flash or hardware test is claimed.
