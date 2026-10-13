# Open-loop motor control from board inputs

**v0.4.1 wiring change:** INLB now comes from **PE10 / CN12 pin 47**, replacing PB0 / CN11 pin 34. Move that wire with power off before energizing the driver. See [phase-B diagnostics](PHASE_B_PWM_DIAGNOSTIC.md).

Firmware v0.4.1, updated 2026-10-13. Current CubeIDE header defaults select this profile. The user reports seeing MCU PWM and complementary switching; measured deadtime and driver/motor operation remain pending.

This uses one RUN switch/jumper and one speed pot. No serial command, Ethernet, encoder feedback, CALIBRATE or ZERO is required. Console/network/encoder transactions are not started in this profile. Shared BSP peripheral setup remains. The existing sinusoidal voltage commutation is reused; identical scope-test duties would produce no rotating field.

## Configuration

```c
#define DEMO_PWM_SCOPE_TEST 0      /* Motor drive, rather than identical-duty MCU scope test. */
#define DEMO_DEFAULT_OPEN_LOOP 1  /* Analog speed and a rotating field. */
#define DEMO_OPEN_BOARD_CONTROL 1 /* PG2 physical RUN owns ARM; no console needed. */
```

These are already set. Refresh/build/flash the active Senior_Design_DEV_FW project normally in CubeIDE. Let the MCU run freely; do not halt while the driver/motor is energized. STM32 watchdog and hardware nFAULT break remain enabled.

## Control wiring

| Function | Nucleo | Connection / behavior |
| --- | --- | --- |
| Speed pot | PA3, A0 / CN9 pin 1; also CN12 pin 37 | 0–3 V, common GND; <=100 mV commands zero, including the reported 40 mV minimum |
| Physical RUN | PG2, CN11 pin 42 | HIGH=3.3 V requests ARM; LOW/disconnected disables. Internal pull-down configured; add about 10 kOhm to GND for a bench harness. A 1 kOhm series resistor to the 3.3 V switch/jumper source avoids a hard rail short while testing LOW. |
| Driver ENABLE output | PG4, CN12 pin 69 | EVM J5 pin 9. Firmware drives this; do not force it externally. Retain effective ~10 kOhm pull-down at driver. |
| Driver nFAULT input | PE6, CN11 pin 62 | EVM J5 pin 15; active LOW hardware shutdown. Retain effective 3.3 V pull-up. This is real driver fault feedback, not a user RUN switch. |
| Logic supply / ground | Nucleo 3.3 V / GND | EVM J5 pin 1 VDDIO / J5 pin 4 GND; common analog reference. Verify rail straps/population. |

Connect the six PWM wires (v0.4.1 moves INLB from PB0 to PE10) (PE9/PE8, PE11/PE10, PE13/PE12 to INHA/INLA, INHB/INLB, INHC/INLC). Use DRV8304H hardware MODE=AGND/6-PWM and CAL low. Record GAIN/IDRIVE/VDS settings. Encoder can remain disconnected. Motor windings connect to EVM A/B/C; the winding supply is 24 V at the motor-power terminal, independent of encoder/MCU logic supply. See BOARD_CONFIG.md for every header location.

Remove any PE6 test fixture that overrides/masks the actual fault signal. A pull-up resistor is appropriate for the open-drain output; a hard tie to 3.3 V is not. Do not repurpose nFAULT as RUN during motor operation.

## Start, stop and restart

1. Secure the unloaded motor and establish the bench power/current limit and cutoff. Start with RUN LOW/disconnected and pot at minimum (firmware ADC <=100 mV). Power/reset the Nucleo; it remains disabled. Connect winding leads only with motor power off.
2. Before a winding connection, use the same RUN/pot sequence to scope the powered EVM gates with the motor disconnected. Verify GH minus SH using an appropriate differential probe, low-side gate-to-source, non-overlap and inhibition. Follow BENCH_SWITCHING_VALIDATION.md stage 2; MCU PWM alone does not verify MOSFET switching.
3. Once power-stage checks pass, power down/connect the motor, restore current-limited 24 V, and keep RUN LOW for at least 20 ms. Raise RUN to 3.3 V for at least 20 ms with the pot at minimum. Yellow LD2 turns on when armed (OPEN_IDLE); ENABLE/PWM remain off until speed is requested.
4. Raise the pot gently, initially around 0.3 V (3 RPM commanded). Crossing 100 mV begins driver wake (~10 ms, PWM off), fixed-angle alignment (1 V ramps over 0.5 s, total 2 s), then positive rotating field. Speed ramps at 10 RPM/s; field voltage ramps toward 2 V. Allow the alignment period before expecting continuous rotation. Alignment can initially move either direction.
5. RUN LOW disables PWM/ENABLE on the next nominal 1 ms control tick, with a final GPIO check before authorization. This is a software bench stop, not a certified hardware emergency stop or brake; the motor coasts. A raw LOW stops immediately at sampling; 20 ms LOW then 20 ms HIGH are required to rearm.
6. Pot back to <=100 mV ramps commanded speed to zero then coasts to OPEN_IDLE. With RUN still HIGH, raising the pot again redoes wake/alignment/run. To revoke authorization, lower RUN.

Boot/reset with RUN already HIGH never arms. Lower RUN for 20 ms, set the pot to minimum, then raise RUN for 20 ms. A HIGH edge while the pot is above 100 mV, ADC invalid/stale, or the controller is not disabled is consumed/refused; lowering only the pot afterward does not arm. Cycle RUN LOW -> HIGH again. Low/high timing uses unsigned elapsed arithmetic across tick wrap.

Red LD3 means a latched software fault. Lower RUN, investigate/remove the cause, turn the pot to minimum, and press RESET. Then make a fresh RUN transition. Releasing nFAULT or cycling RUN alone cannot clear/restart a fault. There is no GPIO fault-clear action. Closed-loop behavior remains the previous console/encoder workflow after selecting DEMO_DEFAULT_OPEN_LOOP=0 and rebuilding. Set DEMO_OPEN_BOARD_CONTROL=0 for the prior serial-armed open-loop path. Runtime mode switching is disabled in this standalone profile; select before flashing.

## What the pot controls

Commanded RPM = V/3 * 30 above the 100 mV deadband. Thus 0.3 V=3 RPM, 0.75 V=7.5 RPM, 1.5 V=15 RPM, 3 V=30 RPM. This controls rotating-field speed, not measured rotor speed or duty magnitude. The encoder is not used to prove rotation. Positive phase sequence is fixed; physical direction depends on winding order. Power off before swapping phases.

The fixed 1 V alignment/2 V run effort and provisional 30 RPM limit are configurable starting points, not motor ratings or a calibrated current limit. Low-side/phase/bus ADC feedback is still unused, so current and loss of synchronism are not regulated/detected. Watch current and unloaded rotor motion; disable if it stalls or behaves unexpectedly. The bench supply's current limit may collapse the bus and cause undervoltage; distinguish that from a timing fault.

To repeat MCU-only scope tests, set DEMO_PWM_SCOPE_TEST=1 and rebuild: driver ENABLE is forced LOW, identical duty follows the pot, and PE6 again acts as a diagnostic permit only with the driver disconnected. Return it to 0 before motor testing.

References: [ST UM2407 connectors/LEDs](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf), [TI DRV8304 6-PWM/wake/fault behavior](https://www.ti.com/lit/ds/symlink/drv8304.pdf). Configured timing/voltage expectations still require physical confirmation.

## User-reported gate-driver observations — 2026-10-13

The user observed complementary MCU PWM and faster duty-envelope modulation as pot voltage increased. With the EVM connected, GLA-to-GND switched to about 10 V (approximately -150 mV minimum), while GHA-to-GND initially stayed near 0 V (approximately -400 mV minimum). After setting hardware MODE to GND, the user reported GHA-to-GND reaching 34 V with a stated 24 V bus. This is consistent with proper high-side drive, and the MODE configuration was likely responsible for the prior behavior; it is not a direct gate-to-source measurement. Actual simultaneous SHA voltage, GHA-SHA waveform, high/low non-overlap, supply current, other phase gates and loaded switching remain unverified. No scope captures or agent hardware measurements were provided. Motor connection/spin acceptance remains pending.

## First motor hookup — 2026-10-13

The user identifies three winding leads as red/black/yellow. For this encoder-free open-loop test, choose and record red=A, black=B, yellow=C at EVM J2; this is a suggested arbitrary mapping, not a verified manufacturer color code. All three are switched phase leads: none is a direct 24 V or ground connection. Interchanging any two winding leads reverses the rotating-field direction relative to the rotor; only change connections with bus power off and discharged. Once closed-loop sensing is introduced, repeat calibration after a phase-order change. Encoder leads are a separate 3.3 V interface and may remain disconnected here.

Before applying motor power, use a meter to compare the three lead-pair resistances (similar finite values, accounting for meter/lead resistance) and check no winding lead conducts to the motor housing. Reconfirm MODE=AGND, INLB=PE10, gate-to-source non-overlap for all phases, RUN low/reset inhibition and actual nFAULT shutdown. Use an unloaded secured motor with free rotation, current-limited 24 V and accessible cutoff. A 0.5 A bus current limit is a conservative initial bench trial setting, not a motor rating or a regulated phase-current limit; supply limiting can cause bus collapse/undervoltage. Start at the reported 40 mV pot minimum with RUN low, arm with a fresh RUN transition, then try about 0.3 V (3 RPM commanded), allowing the 2 s alignment and slow rotation. Observe current/motion and stop for unexpected heating, repeated jerking or stall. Recheck VGS/ringing/non-overlap with the motor attached. Manufacturer specifies 24 V/1.4 A rated and 4 A maximum; the firmware still does not regulate phase current. [DFRobot FIT1036](https://wiki.dfrobot.com/fit1036/).
