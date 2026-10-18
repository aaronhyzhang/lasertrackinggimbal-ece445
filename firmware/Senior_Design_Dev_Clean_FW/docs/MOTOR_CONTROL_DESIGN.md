# Motor control design discussion

Updated: 2026-10-08. Status: encoder-oriented sinusoidal voltage effort with position P / velocity PI is implemented in DEV_FW; physical validation pending. Current-regulated FOC remains the later loaded-gimbal target. See the [firmware guide](../../Senior_Design_DEV_FW/docs/FIRMWARE_GUIDE.md) for actual states, limits, gains and tuning.

## Scope and recommendation

Initial milestone: one unloaded DFRobot 4015/AS5600 motor driven by NUCLEO-H753ZI and BOOSTXL-DRV8304H; 0–3 V analog commands bounded 0–360 degree position. Ethernet follows analog success. Demo is mid next week. Custom H743ZIT6 PCB later drives two gimbal axes and includes phase-current sensing, phase-voltage sensing and AS5600 feedback (user confirmed sensing).

Recommended progression: encoder-based sinusoidal voltage control -> position demo -> Ethernet -> measured d/q current-controlled FOC. Brief bounded open-loop alignment/rotation is a bring-up operation, not the final controller. Six-step is a viable alternative, but not the recommended default for smooth low-speed gimbal position/hold. Full current-controlled FOC is the final architectural target, but current-sensing integration should not block a conservative unloaded proof of concept if hardware protections and bounded drive are verified.

This recommendation follows the demo timing and low-speed position use case; it is not a claim of measured performance. Research against vendor examples is documented in [ARCHITECTURE_RESEARCH.md](ARCHITECTURE_RESEARCH.md). DFRobot's [FIT1036 position example](https://wiki.dfrobot.com/fit1036/docs/24648) uses AS5600, 11 pole pairs and angle P/velocity PI with voltage limits; its Arduino/3-PWM implementation and numerical settings are not a HAL/6-input backend. No library integration or H7-specific driver/current-sampling backend has been validated. SimpleFOC documentation is used to explain the architecture, not to assume a drop-in Arduino integration in the current CubeIDE HAL project.

## Distinct concepts

- Commutation maps rotor electrical angle plus effort demand into phase excitation: six-step or continuous sinusoidal/vector drive.
- PWM modulation converts desired phase voltages into bridge switching duties: sine PWM or space-vector PWM. These are distinct from controller feedback loops.
- Control loops regulate measured position, speed and current. Position feedback alone does not imply current-regulated torque.
- Power/gate hardware applies those duties, manages switching and protection, but does not automatically implement the motor position loop.

## Alternatives

| Scheme | Inputs/control effort | Position-demo tradeoff |
| --- | --- | --- |
| Open-loop rotating field | Time-generated electrical angle and bounded voltage | Easy first-motion check, but shaft can lag/stall; not validated closed-loop position |
| Encoder-based six-step | Electrical-angle sector selects phase pair; signed voltage/current demand sets torque direction | Small commutation table; viable encoder position servo, but discrete vectors can cause torque ripple and hunting around hold/reversal; does not eliminate calibration or outer-loop tuning |
| Encoder-based sinusoidal voltage control | Encoder electrical angle; signed q-axis voltage demand; usually d-axis voltage zero; sine PWM initially | Smoothly varying excitation; good unloaded low-speed demonstration; no current loop, so effort/torque varies with resistance, speed, bus voltage and load |
| Encoder-based current FOC | Electrical angle plus calibrated phase currents; d/q PI controllers produce voltages | Best final target for controlled effort and disturbance rejection; more work in ADC/PWM timing, polarity/scaling, transformations and tuning |

Six-step uses six excitation sectors per electrical revolution, not six commanded mechanical positions. With 11 pole pairs (if FIT1036), this is 66 sectors/mechanical revolution, or about 5.45 mechanical degrees per sector. This does not impose a universal 5.45 degree encoder-servo accuracy limit; it describes vector switching intervals. Verify actual motor pole pairs on the bench. Back-EMF sensorless commutation is not the initial recommendation for zero-speed position hold; use the encoder.

## Feedback loops and controllers

Full cascade:

position target -> trajectory limiter -> position P -> speed target -> velocity PI -> q-axis current target -> d/q current PI -> inverse transforms/PWM -> motor.

- Position controller: compare desired and measured mechanical angle, output desired speed, saturate at the allowed speed. Start with P; do not add integral to every loop automatically.
- Velocity controller: compare speed target with filtered measured speed, output bounded current/effort. PI handles sustained load; reset/clamp integrator when disabled or saturated and add anti-windup that accounts for effort actually applied after downstream voltage/duty/sampling-window limits.
- Current controllers: q-axis current mainly produces torque; set d-axis target to zero for initial low-speed permanent-magnet operation. Two PI controllers command q/d voltages. Torque is approximately proportional to q current for the applicable motor model; calibration is needed before claiming torque in physical units.
- Encoder feeds mechanical position, estimated velocity and commutation angle. Current sensors feed the inner electrical loop. A camera-based tracking controller is a later outer loop on the laptop.
- An alternative position PD controller outputs effort directly, with measured velocity providing damping. It can simplify an unloaded bench servo but makes loading/gain changes more coupled. Cascaded P position + PI velocity is the recommended initial structure.

For the voltage demonstration, retain position P and velocity PI but output signed q voltage instead of q current. Bypass the current PI block. Continuously orient the commanded voltage using encoder electrical angle. This is often called voltage-mode FOC, but it is not measured-current FOC or precise torque control. With no phase-current regulation, conservative voltage/duty limits, verified driver protection and bounded unloaded testing remain necessary. Current monitoring/tripping, where validated, is separate from a closed current loop; a slow software current monitor is not sufficient short-circuit protection.

## Angle and alignment

- Mechanical angle: shaft position used for 0–360 degree demo trajectory; preserve bounded endpoint/unwrapped coordinates.
- Electrical angle: wrap(pole_pairs * signed_encoder_angle + calibrated_electrical_offset), with sign/offset conventions fixed by implementation.
- Mechanical zero and electrical zero are different. A shaft position of zero does not imply the rotor field is aligned to phase A.
- Calibrate phase order, encoder direction and electrical offset with low-energy alignment and a short known-angle move. Do not force alignment against a mechanical stop.
- In continuous drive, orient the effort relative to the rotor field; q effort corresponds to the quadrature axis. Positive/negative demand changes torque direction, not merely forward/backward commutation-table order.
- A wrong direction/offset can cause weak torque, high current or positive feedback. Establish sign conventions before enabling closed position control.

## What the sensors contribute

| Signal | Initial demo | Full current FOC / later use |
| --- | --- | --- |
| AS5600 angle/status | Essential for rotor angle and shaft servo; velocity derived from timestamped angle differences | Same roles; measure latency/noise and sample age |
| Phase currents | Calibrate/observe and add fault monitoring as feasible; not necessarily in first voltage controller | Essential measured current feedback; Clarke/Park transform -> d/q currents |
| Phase voltages | Optional diagnostics; not a required feedback loop for encoder voltage control | Diagnostics and possible future observer/sensorless work; not required measured phase feedback for basic sensored FOC |
| DC bus voltage | Verify bus and voltage-limit scaling | Modulation normalization, voltage saturation and bus protection |

Schematic review confirms separate motor-bus sensing at PF13 / ADC2_INP2 with a 100k/10k divider (/11), buffered on the initial assembly. Filtered phase-voltage ADC readings are not instantaneous winding voltages; PWM state, neutral/common-mode voltage, divider/filter delay and sampling matter if used in an observer.

For a three-wire motor with no neutral current, two valid phase-current measurements can reconstruct the third via ia+ib+ic=0. Three shunts still help redundancy and choice of valid sampling windows. Low-side shunts only produce valid readings in appropriate conduction states; trigger ADC after switching transients/amplifier settling and before the valid window closes. At high modulation the window can disappear, requiring duty limits or reconstruction. Inline sensing has different common-mode/bandwidth requirements. User confirmed custom shunts in low-side returns, 14 mΩ, 3.3 V reference; proposed amplifier gain 20 and expected maximum current around 4 A. Schematic confirms DRV8304S, 14 milliohm shunts and 3V3_A VREF; verify gain/midpoint registers, zero offsets and sample timing on hardware.

Current conversion: i_phase = (V_ADC - V_offset)/(amplifier_gain * shunt_resistance), including measured sign convention. Confirm offsets at zero current, gain/reference, amplifier output headroom, channel-to-phase mapping and simultaneous/skewed acquisition before closing PI loops.

## Firmware blocks

- board_config: separate H753 Nucleo and H743 custom clocks, pins, polarity, ADC scales, PHY and driver settings.
- gate_driver: separate hardware-strap backend for DRV8304H EVM and SPI/readback backend for custom DRV8304S, with common enable/fault and verified PWM-mode semantics. Wake/configuration permission is separate from motor actuation.
- pwm_driver: one advanced timer for one inverter, synchronized duty updates, complementary timing/dead time and shutdown. Confirm 3-PWM versus 6-PWM truth table; prefer representative 6-PWM if final PCB uses it. Sine modulation first; SVPWM later if needed.
- encoder: AS5600 transaction/status, direction, unwrap, timestamps, velocity filter and stale/failed read detection.
- analog_command: calibrated voltage mapping to 0–360 degree target, filtering and separate enable.
- trajectory: bounded position, velocity/acceleration limits and endpoint/reference handling.
- motion_control: position P and velocity PI, saturation, anti-windup and disable/reset handling.
- commutation: encoder electrical angle plus signed q voltage initially; later d/q voltage outputs from current controllers.
- current_sense/current_control: timer-triggered sampling, offsets/gain/signs and validity; Clarke/Park and d/q PI added after measurement is validated.
- supervisor: disarmed/calibration/ready/run/fault state machine, sample freshness, voltage/current/temperature limits as available, deadline monitoring and command source ownership.
- telemetry/comms: serial for bring-up, Ethernet later; report input/target/actual angle, speed, q voltage/current, bus, faults and missed deadlines. Parsing/logging remain outside motor ISR.

## Timing proposals — verify rather than assume

Candidate initial PWM: center-aligned around 20 kHz. Candidate encoder and voltage-commutation update: about 1 kHz for intentionally slow demo motion, subject to measured transaction/filter delay and angular lag. Candidate velocity loop 500 Hz–1 kHz and position loop 100–250 Hz. These are scheduling starting points, not promised bandwidths. Hold PWM duty between updates; hardware PWM continues independently.

Electrical-angle lag is approximately pole_pairs * mechanical_speed * feedback_delay. For 11 pole pairs, 60 RPM and 1 ms total angle age produce about 3.96 electrical degrees of lag; total sensor/filter/I2C/scheduling delay must be measured. PWM carrier rate does not make a cached angle sample fresh. Avoid blocking I2C from a 20 kHz ISR; use bounded asynchronous acquisition/cached timestamped data and reject stale feedback. Any extrapolation must be bounded and validated.

Current FOC later can run synchronized current acquisition/control at roughly 10–20 kHz with adequate electrical-angle freshness and slower motion loops. Establish worst-case execution and valid sampling windows first. Tune inner loops before outer loops; the fast loop needs substantially higher bandwidth than its enclosing loop, but actual bandwidth is constrained by sensing, delay, switching and mechanics.

## Bench implementation sequence

1. Verify PWM enable/fault/dead time and driver modes without motor; acquire analog and hand-rotated encoder data.
2. Verify current/bus sensing scales and basic protections before substantial energization.
3. Low-energy alignment/open-loop move to confirm phase order, encoder sign/pole pairs and electrical offset.
4. Test encoder-based signed q-voltage effort in both directions with a strict drive cap; check commutation follows rotor.
5. Tune velocity controller first, then position P with bounded trajectory; check hold, reversal and 0/360 endpoints.
6. Demonstrate 0–3 V analog position input; log angle error/settling and current/temperature where measurable.
7. Add Ethernet command source without changing servo/commutation logic.
8. Validate synchronized phase-current acquisition and current transforms; check d/q signs on gentle known excitation.
9. Close d/q current PI at low limits, retune velocity output in amperes, then recheck position and load response.

## Sources

- [ST six-step overview](https://www.st.com/en/applications/industrial-motor-control/3-phase-6-step-control.html): block commutation and rotor feedback/current-mode options.
- [SimpleFOC position control](https://docs.simplefoc.com/angle_loop): cascade versus direct effort architecture.
- [SimpleFOC voltage mode](https://docs.simplefoc.com/voltage_torque_mode): encoder-oriented voltage control and its lack of current regulation.
- [SimpleFOC current mode](https://docs.simplefoc.com/foc_current_torque_mode): d/q feedback, PI voltage outputs and torque-current relationship.
- [SimpleFOC sensor alignment](https://docs.simplefoc.com/alignment_procedure): rotor/encoder direction and offset.
- [SimpleFOC low-side sensing](https://docs.simplefoc.com/low_side_current_sense): PWM/ADC synchronization.
- [TI sensored FOC reference](https://www.ti.com/lit/pdf/tidub03): sensored PMSM FOC and staged integration.

## Current-sense parameters and remaining hardware questions

User confirmed low-side return shunts, 14 mΩ, 3.3 V reference, expected maximum current around 4 A, and proposed gain 20. Sensitivity is G*R = 0.28 V/A. At ±4 A the output shift is ±1.12 V; if zero current is VREF/2 = 1.65 V, ideal output spans 0.53–2.77 V. Ideal rail-based range is ±5.89 A; the DRV8304 specified 0.25 V to VREF–0.25 V output interval instead gives nominal ±5 A at this gain, before tolerances. 4 A is an expected operating figure, not an approved protection trip threshold; confirm whether it means phase peak, RMS or supply current. At 4 A instantaneous shunt dissipation is I²R = 0.224 W while conducting; check PWM/RMS heating, rating and layout.

All nine pages of Schematic PDF_[No Variations] (1) (1).pdf were reviewed on 2026-10-07. See [HARDWARE_REFERENCE.md](HARDWARE_REFERENCE.md) for custom DRV8304S, pin/channel bindings, buffers and current/voltage filters. User confirms the sensor-buffer bypass jumpers, U6 backup regulator and R7 EN-to-ground resistor are initially unpopulated. Custom drivers share nFAULT on PE6 and have ENABLE pull-ups; reset/shutdown needs physical validation. Driver sense-overcurrent minimum 0.25 V across 14 milliohm is about 17.9 A, so it alone cannot establish the expected 4 A limit. Nucleo EVM shunts/gain must be calibrated independently; do not copy custom-board 14 mΩ scaling to the EVM. User confirmation of the recommended voltage-first/current-FOC-later architecture is still pending.

## Firmware architecture

See [FIRMWARE_ARCHITECTURE.md](FIRMWARE_ARCHITECTURE.md) for proposed runtime scheduling, interfaces, source layout and implementation slices.

Schematic/vendor verification source: [TI DRV8304 datasheet](https://www.ti.com/lit/ds/symlink/drv8304.pdf). New H753 target review: [DEV_PROJECT_REVIEW.md](DEV_PROJECT_REVIEW.md).

## Research refinements — 2026-10-07

- Use DEV_FW as the implementation target. Current-loop integration and H743/two-axis porting remain later work.
- AS5600 filter settling is configuration-dependent (0.286–2.2 ms); do not treat poll/completion rate as physical measurement rate or settling time as pure lag. Measure effective commutation lag and bound speed accordingly.
- At 1 kHz/12 bits, one count per sample is approximately 1.53 rad/s in a naive velocity derivative. Use a timestamped slope/filter and account for lag when tuning.
- Keep [TI's staged validation and inner-before-outer tuning](https://www.ti.com/lit/an/sprabz0a/sprabz0a.pdf), adapting the voltage-only demo as a project-specific intermediate stage.
- PE6 supports TIM1_BKIN2. Configure/measure it; establish TIM8 fault shutdown before enabling two axes.
- Two-axis current control needs one shared ADC1/2 scheduler, valid phase-pair windows and coherent per-axis sample contexts. See ARCHITECTURE_RESEARCH.md.
- Consider a pinned MCSDK single-axis prototype before full current FOC; vendor H7 family support does not prove the exact sensor/driver port or dual-drive support.

## Implementation record — 2026-10-08

Confirmed motor FIT1036, 24 V bus, ST-LINK VCP. One-axis HAL-independent control core now implements timed alignment/sign verification, unwrapped mechanical zero/travel, modulo electrical angle, bounded trajectory, P-position/PI-velocity, conditional anti-windup, sine modulation and freshness/driver/tracking/travel faults. Starting values: 11 pole pairs, 1 V alignment, ±2 V q effort, 0.4 rad/s, 0.4 rad/s². All loops run at 1 kHz in this first profile; TIM1 carrier is 20 kHz nominal. Encoder/ADC current sensing and physical protection/tuning remain distinct.

The portable C tests validate both directions, trajectory reversal and speed/acceleration/effort bounds, calibration failure, stale data/commands, latched faults and angle/time wrap. They do not establish physical torque/current regulation. EVM current shunt/protection configuration must be measured separately from the custom 14 mΩ/gain-20 assumptions.

## Open-loop staged validation — 2026-10-12

Implemented a generated-angle open-loop speed branch alongside the retained closed-loop position controller. Analog maps 0–3 V to 0–30 RPM provisionally, with positive-only field advance, acceleration and low-voltage startup. It does not depend on encoder samples and cannot verify rotor synchronization, stall, current or travel. Runtime mode changes require inhibition, clear calibration/command authority and require a fresh explicit ARM; closed-loop return requires calibration/ZERO. Separate default-open build images share the same source/BSP. [OPEN_LOOP_GUIDE.md](../../Senior_Design_DEV_FW/docs/OPEN_LOOP_GUIDE.md) includes hardware assumptions, tests, and primary TI/DFRobot/official SimpleFOC references.
