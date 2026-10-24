# J3 / J4 firmware comparison after replacement hardware

2026-10-24. Scope and firmware audit; no control/profile change or hardware flash.

## Evidence and conclusion

The user reports J3 spun its motor successfully. J4 passes DRIVER_CHECK without PWM, and its six MCU signals pass PWM_CHECK. MOTOR_RUN produces a first B-high input pulse followed by physical nFAULT assertion, then MCU PWM inhibition. The user reports phase B at J4 does not move during that command. A replaced B-high MOSFET and then a replaced driver IC did not change the symptom; gate-path continuity and no shorts between MOSFET nodes were reported. These reports do not establish gate voltage or current during switching.

Latest screenshot: configuration verified, normal profile [0,0,0x080,0x300,0x300,0x300,0x280]; first trip NFAULT_EDGE, sampled low, ready/authorized=true, wake age=21 ms; capture COMPLETE, mask=3, R0=1544 (0x608), R1=0, capture=110 us. TI decodes 0x608 as FAULT + VDS_OCP + VDS_HB. This is still the B-high VDS monitor; it is not a measured current or a proven damaged component. The old g_driver_status zeros are the startup status, not the captured fault.

**No axis-specific timer/pin/profile defect was found. Firmware/timing remains a possible cause; this audit does not prove the power stage is defective. Stop substituting components without a distinguishing test.**

## Actual differences

| Function | J3 / axis 0 | J4 / axis 1 |
| --- | --- | --- |
| PWM timer / alternate function | TIM1 / AF1 | TIM8 / AF3 |
| A high / low | PE9 / PE8 | PC6 / PA5 |
| B high / low | PE11 / PB0 | PC7 / PB14 |
| C high / low | PE13 / PE12 | PC8 / PB15 |
| ENABLE / CAL | PG4 / PG5 | PG7 / PG6 |
| nCS | PE5 | PA9 |
| Driver enable / fault indicators | PE3 / PE2 | PC10 / PC11 |
| PE6 shared nFAULT | AF1 TIM1_BKIN2, active-low hardware break plus ISR | Port E EXTI6 falling-edge, priority 0; ISR clears TIM8 MOE |
| Hardware shutdown latency | Timer break path; not measured | GPIO interrupt path; not measured |

The inactive bridge has ENABLE low, CS high, CAL low and all six PWM pins GPIO-low. nFAULT is shared, but the selected J4 driver's captured B-high flag is separate evidence beyond the shared line. J4 has no direct TIM8 break on PE6. Changing interrupt latency changes the MCU response after assertion; it cannot by itself write a VDS_HB flag into the driver. Configuration and output timing can still provoke a real flag.

Schematic physical pages 3/5/6/7 and ST DS12110 Table 10 agree with these bindings. Both inverter sheets have corresponding circuits, with GHB -> 10-ohm R27 -> Q2 gate and SHB -> B midpoint; suffix _I_0 versus _I_1 selects the instance. Schematic equivalence is not PCB-layout or assembly verification.

## What is identical

- Both timer kernels are APB2 timers under the same shared clock setup: intended 100 MHz; prescaler 0, ARR=2500, center-aligned mode 1, nominal 20 kHz. No TIM8-specific divider is selected.
- Both use PWM1, active-high main and complementary outputs, three channel pairs, compare preload, idle-low states, nominal 500 ns timer dead time, and MOE initially off. The same compare-writing helper selects TIM1 or TIM8. No special J4-B duty calculation exists.
- VR1, S1/S2, neutral guard, 20 ms wake, 2 s alignment, voltage ramp and signed speed ramp execute the same motor code. Initial compare values are all 50%; the initial alignment references are near 50% with B and C equal at angle zero. MOTOR_RUN does not intentionally suppress B only.
- SPI2, clock/mode/frame format, configuration writes, readback checks and all profile values are common. The only axis choice in the SPI route is chip select. Driver configuration holds COAST during writes, releases it after settings, then verifies.
- Profile: 6-input PWM; source/sink drive 15/30 mA, TDRIVE=4 us, driver dead time=400 ns, VDS threshold=0.15 V, latched all-bridge OCP, CSA gain=20. These are not independently tuned for J3/J4. The 0.15 V threshold is across the MOSFET, not the 14-milliohm shunt. The driver's VDS deglitch is fixed at 4.5 us; it is not a tunable SPI field.

Gate-drive strength and VDS threshold can influence switching faults even with correct MCU signals. At Vishay's specified max Qg=29 nC and 15 mA, Q/I is about 1.93 us; this estimate is below the 4 us TDRIVE, but does not establish actual Miller/source motion, supply conditions or noise margin. Do not treat the lowest drive setting as proven correct merely because J3 works. No drive strength or protection threshold was changed in this audit.

## PWM_CHECK versus MOTOR_RUN

For axis 1, the actual TIM8 PWM register setup is the same in these two stages. PWM_CHECK keeps the external bridge disabled and deliberately supplies driver_good=true to the motor model. MOTOR_RUN raises the selected ENABLE, configures/verifies the driver, requires live nFAULT high, and enables the EXTI shutdown policy. Both ultimately set the same timer MOE bit to release the PWM outputs. STOP, timing, stale ADC and software-fault inhibits remain in both.

Before output release, MOTOR_RUN clears stale EXTI pending state and rechecks the live fault line. The captured NFAULT_EDGE source and physical scope falling edge are not the earlier readiness-rejection path. The scope showed B-high becoming high before nFAULT; this is not evidence that no PWM started. A fault can truncate the first pulse before the next 50 us carrier period.

The bench capture setting only acts after a trip: all PWM is inhibited, the already-awake selected ENABLE is retained briefly for read-only status frames, then lowered. It is shared by both axis implementations and does not cause a B-high command before the event. Capture=1 is for disconnected motors only.

## Startup limitation found in both implementations

`pwm_init()` enables the counter with MOE off. `board_apply()` later writes preloaded compares and sets MOE without resetting the counter or forcing an update while outputs are inhibited. Therefore the first exposed PWM interval can be partial, and the new compare triplet is not guaranteed active until the next timer update. The initialized compares are 50%; the alignment references start close to 50%. This is not a B-only or TIM8-only code branch, but counter phase can differ with execution timing between builds and wake/configuration paths.

This is a legitimate controlled firmware experiment, not an established explanation for a fixed B-high VDS fault. A synchronized start could stop the counter with MOE off, load the three compares, commit via update, reset to a known count/direction, then release the outputs/counter. Before changing it, preserve the current working J3 baseline and compare both axes under the latest identical settings. Tests here do not emulate counting, preload transfer, gate drivers or the first analog switching edge.

## New timer-initialization check

A Windows private-RAM test executes actual `board.c` GPIO/PWM initialization plus the actual vendor GPIO, TIM and TIMEx HAL. Four variants pass: J3/J4 x PWM_CHECK/MOTOR_RUN, capture=0. CPU/NVIC and EXTI W1C behavior are simulated. Clock-tree setup, register hardware side effects, GPIO BSRR-to-ODR updates, timer counting, preload transfer and real IRQ timing are not emulated. Unused peripheral service stubs abort if reached. Assertions are explicitly enabled.

| Initialized / authorized register | J3 PWM | J3 MOTOR | J4 PWM | J4 MOTOR |
| --- | --- | --- | --- | --- |
| CR1 | 0xA1 | 0xA1 | 0xA1 | 0xA1 |
| CCMR1 / CCMR2 | 0x6868 / 0x68 | same | same | same |
| CCER | 0x555 | 0x555 | 0x555 | 0x555 |
| Initial BDTR, MOE off | 0x2D32 | 0x1002D32 | 0x2D32 | 0x2D32 |
| DIER after authorization | 0 | 0x80 (break IRQ) | 0 | 0 |
| AF1 / AF2 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |

The J3 MOTOR BDTR difference is intended BK2E. The test also verifies PSC=0, ARR=2500, RCR=0, all initial CCR=1250, all output polarities, output authorization, compare writes and inhibition. These checks extend the earlier GPIO/EXTI simulation, which did not execute the real timer HAL. They are not a live MCU register dump and cannot prove physical correctness.

## Next discriminating bench comparison

1. Both motors disconnected, same board/supply/probes. Retain MOTOR_RUN, strict CPUV history=0 and capture=1. Change only PCB_TEST_AXIS from 1 to 0; clean/rebuild/flash the existing project. Confirm live g_app_status.stage=3 and test_axis=0. This tests J3 with the latest fault-capture firmware, not the older successful image.
2. Observe J3 first B-high input TP19, B-low TP20 and J3 phase B; nFAULT is still PE6, ENABLE is now TP30. Compare to J4 TP25/26 and TP34. Use the same acquisition settings. Do not connect a motor with the capture diagnostic image.
3. Record whether J3 switches and its first pulse width/startup relationship. If J3 now also faults, investigate common startup/profile changes before local hardware. If it works, timer/pin/interface differences and local gate/sense behavior remain; a controlled synchronized-start comparison is the next firmware experiment. No repeated rework or protection bypass is warranted by this audit.

The working J3 result is historical; a latest-image J3 result has not yet been supplied. No agent flash or physical test is claimed. Active configuration was left J4/MOTOR_RUN/history=0/capture=1.

## Primary references

- ST DS12110 Table 10 (TIM1/TIM8 pin functions): https://www.st.com/resource/en/datasheet/stm32h743ai.pdf
- TI SLVSE39B sections 7.3.1.4, 7.3.5.3, 7.6 (gate drive, VDS detection, profile/register decode): https://www.ti.com/lit/ds/symlink/drv8304.pdf
- Vishay SiS890ADN electrical characteristics (gate charge): https://www.vishay.com/docs/78019/sis890adn.pdf
- Local reviewed nine-page schematic, SHA-256 2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144.
