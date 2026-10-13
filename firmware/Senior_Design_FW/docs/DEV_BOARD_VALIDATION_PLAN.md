# Dev-board firmware and motor validation plan

Updated: 2026-10-12 (America/Chicago)
Status: dev firmware implemented and software-tested; electrical and motion acceptance pending. See [firmware guide](../../Senior_Design_DEV_FW/docs/FIRMWARE_GUIDE.md) and [validation record](../../Senior_Design_DEV_FW/docs/VALIDATION.md).
Owner: Colin. This file is the persistent record of demo intent, decisions, test gates, and bench results. Update it when requirements change or a bench session finishes. Proposed choices below are not confirmed requirements.

## Confirmed intent

- User confirmed FIT1036, 24 V motor bus and ST-LINK virtual COM on 2026-10-08.

- Validate basic MCU firmware and motor control on dev boards while custom PCBs are pending, before bringing up the custom boards.
- Confirmed MCU board: NUCLEO-H753ZI with STM32H753ZIT6; physical board revision pending.
- Confirmed custom PCB MCU: STM32H743ZIT6. Reviewed schematic confirms LAN8742A-CZ Ethernet PHY, TCAN1044AVDRQ1 CAN transceiver and two DRV8304SRHAR inverter channels. See HARDWARE_REFERENCE.md for mappings and assembly notes. Separate board configurations are required; share application/control code.
- Confirmed motor driver: BOOSTXL-DRV8304H (hardware-configured H variant).
- Final system has two motors driving a two-axis gimbal; planned motors are DFRobot 4015 with AS5600 encoders. FIT1036 confirmed; initial demo uses one motor/driver.
- Initial demo scope: dev boards and one motor only. Latest milestone order: analog position control first, then Ethernet position commands if analog bring-up succeeds. Position travel is 0–360 degrees. Camera, vision and assembled two-axis gimbal are outside this demo.
- Analog milestone confirmed: an available ADC input reads 0–3 V, corresponding linearly to 0–360 degrees. PA3 / A0 / CN9 pin 1 is implemented; physical harness verification remains required.
- Confirmed demo transport: Ethernet after analog success. Final laptop-to-PCB transport may be Ethernet or CAN; selection remains open.
- Final system consists of a camera, laptop, PCB and two motors. The laptop sends an offset/vector or derived motion targets to keep an object within 80% of the camera ROI. Camera is mounted on the gimbal; the entire object must remain inside the central 80% region. Command semantics remain undecided.
- Keep reusable firmware and a reproducible test record to support transfer to custom hardware.

## Later system context — outside the initial demo

Confirmed system: camera + laptop + PCB + two motors; two-axis gimbal. Goal: keep the tracked object within 80% of the camera ROI. Laptop computes tracking information and sends corrections over Ethernet or CAN. Do not assume inertial stabilization or an IMU requirement; base-motion compensation is not specified.

Proposed division of work:

- Laptop: capture frames, select/detect target, estimate image error, apply tracking deadband and compute bounded pan/tilt angle targets. A laptop-generated absolute target is the initial proposal, not a user-confirmed interface.
- PCB: local encoder-based position control for both axes, trajectory/rate limits, phase-current/drive limits, arming, fault handling and command-age monitoring. Position control continues locally between laptop updates.
- Keep raw image errors out of the MCU motor-control interrupt. If raw offsets are ultimately chosen, specify normalization, image orientation, camera intrinsics/FOV, axis signs and conversion/controller ownership explicitly.
- Include both axis targets in one logical command with explicit units, sequence number, session identity, validity and command age. Handle duplicate/out-of-order commands and reset/reconnect. Relative increments must not be applied twice; absolute targets are easier to make idempotent.
- For CAN, plan a laptop CAN adapter, board-side transceiver, physical wiring/termination and payload/rate budget. If two-axis commands span frames, use a transaction sequence and apply only a complete pair. Dev-board CAN hardware availability still needs confirmation.
- Timestamp image capture, detection completion, command transmission/receipt and motor telemetry. Cross-device one-way latency requires clock synchronization or another defined measurement method; do not subtract unrelated clocks. Measure the complete image-to-motion delay and jitter before selecting tracking gains.
- Target lost and communication lost are separate conditions. Proposed target-loss response: stop generating new corrections and hold the last permitted target for a bounded interval while reporting loss; final hold/park/disarm policy must account for gravity and available holding effort. Driver/encoder faults disable the affected power stage; decide whether both axes stop together. Never chase stale detections.
- Confirm camera placement, axis geometry, gear ratio/direct drive, mechanical travel, cable limits, camera mass/inertia and balance. Loaded vertical-axis holding is a separate test from unloaded bench position control.

User confirmed: camera is on the gimbal; the entire object must stay inside the central 80% region. Use the detected bounding box as the proposed measurable object extent. For an ROI of width W and height H, the proposed boundary is x in [0.1W, 0.9W], y in [0.1H, 0.9H]; all four box edges must fit. Confirm whether the ROI is the whole frame or a detector crop. If the object is larger than the permitted region, pan/tilt alone cannot satisfy containment; report this as an infeasible condition instead of integrating more correction. Command semantics are explicitly undecided; compare laptop-generated angle targets with raw image offsets during interface design.

Additional end-to-end demo progression:

1. Command both axes from a laptop test tool without vision: independent steps, simultaneous moves, travel limits and loaded hold.
2. Replay synthetic image errors or recorded detections to check signs, target conversion, saturation, deadband, stale data and loss/reacquisition.
3. Track a stationary object with the assembled gimbal; confirm each axis moves in the direction that reduces image error.
4. Track an object moving horizontally, vertically and diagonally at defined distance/speed; measure ROI containment and recovery.
5. Repeat with detection dropout, variable frame rate, transport jitter, cable loss, mechanical limits and reconnect. Avoid integral buildup at travel limits and sudden moves on reacquisition.

Record: frame size/FOV/FPS and ROI, target-center/bounding-box rule, percent of valid frames meeting containment, longest excursion outside ROI, reacquisition time, image error, actual/commanded axis angles, end-to-end latency, axis speed/current/temperature and fault state. Define the required containment percentage, test duration and target motion envelope before claiming the system passes; avoid counting frames with no valid detection as successful tracking.

## Motor-control design discussion

See [MOTOR_CONTROL_DESIGN.md](MOTOR_CONTROL_DESIGN.md) for commutation alternatives, feedback loops, sensor roles, firmware modules and tuning sequence. User confirmed custom PCB phase-current and phase-voltage sensing, plus AS5600 position feedback. Proposed architecture: encoder-based sinusoidal voltage control for the initial unloaded analog position demo, then measured-current FOC. The voltage servo is implemented in DEV_FW; current FOC remains a later stage. Custom current shunts confirmed in low-side returns: 14 mΩ, 3.3 V reference, proposed gain 20, expected current around 4 A. Schematic review now confirms custom DRV8304S, 3V3_A reference, three low-side shunts per axis and buffered acquisition. Gain/midpoint, offsets, sampling and actual protection settings still need bench/register verification; see HARDWARE_REFERENCE.md.

## Repository baseline observed

- Senior_Design_FW.ioc, .cproject, startup, and linker scripts target STM32H743ZITx, not H753.
- Core/Src/main.c contains generated initialization and an empty application loop. No motor algorithm, encoder acquisition, command processing, or application network stack was found in the inspected project files.
- TIM1/TIM8 are configured with complementary channels, up-counting, period 65535, zero programmed dead time, and disabled break inputs. PWM is not started by the application. These are placeholders, not a validated inverter configuration.
- ADC1/2/3 each currently convert one software-triggered channel with short sampling time; configured analog pins do not imply all intended signals are sampled.
- Clock setup assumes a 25 MHz HSE crystal and a nominal 75 MHz CPU clock.
- Existing RMII TX_EN/TXD0 pins are PB11/PB12. UM2407 lists PG11/PG13 for the MB1364 onboard PHY. PG11 is currently a GPIO input in the project.
- ETH descriptor section attributes exist in main.c, but the inspected FLASH linker script does not explicitly place those sections. DMA buffers, memory placement, MPU/cache policy, PHY setup, and LwIP integration need deliberate implementation.
- Local reference PDFs already exist in Datasheets_UserManuals: Nucleo databrief, UM2407, and TI SLVUB97.

## Hardware facts and implications

- The implemented demo uses HSI, avoiding dependence on the ST-LINK MCO/HSE population. UM2407 remains the board clock/connectivity reference; any future HSE port must verify actual revision/jumpers and frequency. It also gives the onboard Ethernet pin allocation. [ST UM2407](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf)
- If the driver is BOOSTXL-DRV8304H, it uses LaunchPad headers and a 6–38 V motor supply. TI exposes PWM inputs, enable/fault, current-sense outputs, and bus/phase-voltage sensing. H devices use hardware configuration; S devices use SPI. Build an explicit Nucleo adapter/harness and inspect all straps rather than assuming plug compatibility. The AS5600 is an angle sensor, not three Hall commutation outputs; TI's MSP430 example is a reference, not our STM32 firmware. [TI SLVUB97](https://www.ti.com/lit/ug/slvub97/slvub97.pdf)
- If the motor is FIT1036, DFRobot lists 11 pole pairs, a 3.3 V encoder, and default I2C output. Its resistance and torque specifications contain ambiguities; verify electrical values before using them for tuning. [DFRobot FIT1036](https://www.dfrobot.com/product-3006.html)
- AS5600 provides 12-bit resolution; 360/4096 is about 0.088 degrees per count, not guaranteed angular accuracy. Its filter trades delay against noise. Verify configuration, read latency, sample age, and achievable speed before selecting feedback bandwidth. Multiple AS5600 devices need address separation (e.g. separate I2C buses or a mux) because the address is fixed at 0x36. [AS5600 datasheet](https://look.ams-osram.com/m/7059eac7531a86fd/original/AS5600-DS000365.pdf)
- H7 Ethernet DMA cannot access DTCM. Define DMA-accessible buffer placement and a consistent cache policy, verified in the map file and on hardware. [ST CubeH7 example](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Applications/LwIP/LwIP_HTTP_Server_Socket_RTOS/readme.txt)

## Delivery schedule and available equipment

- Parts expected Thursday, October 8, 2026, at night (tentative).
- Demo deadline: mid next week (approximately October 14–15, 2026); exact time remains unspecified. Proposed internal target: working analog demo by the weekend and integrated Ethernet by Monday, October 12, if analog gates pass, leaving time to rehearse.
- User has access to typical bench equipment: power supplies, oscilloscopes, multimeters, etc. Record actual supply ratings/current limit, probe availability and isolation/grounding before powering the inverter.
- Initial motion is unloaded and mechanically secured. Confirm a full revolution is clear of wiring and fixtures.

| When | Priority and exit criterion |
| --- | --- |
| Before arrival: Oct 7–8 | Prepare separate H753 dev target while retaining H743 custom target; review harness and ADC pin; prepare encoder, 0–3 V mapping, position limits and fault state machine; get a repeatable build |
| Arrival: Oct 8 night, or next bench session | Inventory and inspect boards/straps; verify grounding/rails; flash/debug with gate disabled; read encoder by hand |
| Oct 9 | Scope PWM and shutdown, verify sense scaling, then perform low-energy first motion and electrical-angle calibration |
| Oct 10–11 | First pass analog position moves/hold across 0–3 V with telemetry; then bring up Ethernet/host commands and timeout handling if analog works |
| Oct 12 target | Rehearse analog milestone and Ethernet if ready; capture evidence and freeze known working snapshots |
| Remaining time before actual demo | Repeat cold-start/reconnect tests and fix issues; avoid adding CAN, camera or two-axis scope |

If a gate fails, resolve it before motor energization. Prioritize analog position control; Ethernet is the next milestone. If position or Ethernet integration remains blocked, report the demonstrated subset accurately rather than presenting open-loop motion as validated position control.

## Initial demo definition

Hardware in scope: NUCLEO-H753ZI, BOOSTXL-DRV8304H, one DFRobot 4015/AS5600 motor, bench power and a command host. The laptop is only a command/telemetry host for this demo; no camera/detection pipeline is needed.

First milestone: explicitly enable the motor and vary a measured 0–3 V analog input to command bounded mechanical position; show input voltage, target angle and measured encoder angle over serial telemetry. After analog passes, the host enables the motor, sends a sequence of bounded position targets, observes move/hold, reads encoder position/status, and disables it. Demonstrate loss of communication with a deliberate cable disconnect or stopped heartbeat, using a defined stop/hold/disarm response appropriate to the unloaded fixture. Do not require vision tracking, two-axis mechanics, a complete tracking algorithm or custom PCBs to pass this milestone.

Minimum acceptance: repeatable motion to at least three targets within the permitted travel; commanded and measured position shown in host telemetry; measured error and settling time meet agreed numeric limits; no motion on boot/reconnect until explicitly enabled; fault/communication timeout response demonstrated. Travel is 0–360 degrees. Error, settling time and timeout thresholds remain open; proposed unloaded bench targets are ±5 degrees settled error, settling within 2 seconds for the rehearsed bounded moves, and a 500 ms command heartbeat timeout. Validate/tune these on the bench and record the achieved values; they are proposed criteria, not established system performance.

Proposed command set: ENABLE, DISABLE, SET_POSITION, GET_STATUS and CLEAR_FAULT. Include explicit angle units, bounded target checks, sequence/session handling, heartbeat and actual angle/fault telemetry. Keep the position-command interface common to analog and Ethernet. Select one active source explicitly, with a defined disarmed transition between sources.


Position convention proposal: use a documented mechanical zero and bounded 0–360 degree trajectory coordinates for both analog and Ethernet targets. Preserve distinct commanded endpoints; display modulo encoder angle separately. Establish the reference before arming and initialize the target to current position on enable. Do not auto-home or move to zero on power-up. The analog mapping section below defines the proposed full-range sweep and reset/reference considerations.

## Proposed demo progression and gates

Every stage starts disarmed and advances only after recording its pass evidence. Numeric limits must be selected for the confirmed setup before power-stage operation.

| Stage | Work and visible outcome | Pass evidence |
| --- | --- | --- |
| 0 — Preparation, before delivery | Confirm parts, schematics, harness and pin allocation; prepare Nucleo target, shared interfaces, telemetry, command parser and limit definitions | Reproducible build; reviewed wiring and power plan; host checks for parser/state transitions/angle wrap |
| 1 — MCU baseline | Flash/debug Nucleo; heartbeat; serial diagnostics; reset cause; stable timebase; watchdog; gate enable stays inactive | Repeated boots/resets remain disarmed; timing measured; debugger/error-handler behavior defined |
| 2 — Inputs, no motor power | Read analog command and AS5600; rotate shaft by hand; measure direction, wrap, magnet status, I2C errors and sample age | ADC agrees with measured input across range; valid angle over a revolution; disconnected inputs cause defined behavior |
| 3 — PWM and driver, motor disconnected | Check chosen PWM mode, frequency, polarity, synchronization, complementary timing/dead time, enable sequencing and current/bus sense scaling | Scope captures at MCU/driver inputs; driver disabled on reset/fault; offsets calibrated; no unexpected active drive |
| 4 — First motion | Mount one unloaded motor; low-energy alignment and bounded open-loop electrical-angle ramp; encoder used for observation | Repeatable gentle rotation; logged direction, angle, current and bus voltage; stop/disable works |
| 5 — Closed-loop position validation | Establish electrical zero and encoder direction; validate voltage commutation; tune bounded velocity/position move/hold; add current regulation later before claiming torque control | Measured tracking and limit behavior; repeatable calibration; no feedback loss or deadline overruns |
| 6 — First milestone: analog position | After feedback/calibration, map 0–3 V to 0–360 degrees; separate enable; filter/rate-limit target changes | Meter-verified voltage at 0, 0.75, 1.5, 2.25 and 3 V; target/actual angle telemetry; repeatable move/hold and defined input-loss behavior |
| 7 — Ethernet baseline | Bring up PHY/link and IP stack with motor disabled; static bench addressing proposed; PC command/telemetry tool | Ping/command exchange; reconnect and malformed-message tests; bounded servicing time |
| 8 — Ethernet motor demo | Replace analog command source through the same command interface; retain local motor loop | Start/setpoint/stop/status work; unplug/client loss times out to stop/disarm; flooding does not disrupt control |
| 9 — Transfer to custom boards | Repeat input/PWM/sense checks with new board configuration, then motion and network demos | Comparison against dev-board results; custom-board-specific timing, scaling and faults verified |

Position is the selected control mode. Initial demo order: stages 0–6, then 7 and 8 if analog position works. Stage 6 is the first complete analog milestone; stage 9 and all camera/two-axis tracking work are later milestones. First open-loop rotation remains a separate bring-up check. Implement shaft-angle feedback with a position loop and bounded velocity/drive demand. Encoder-based sinusoidal voltage control can be an intermediate step; without calibrated phase-current feedback it is not validated torque/current control. Begin with low holding effort and unloaded motion. Select target-rate limits, position limits, settling/error criteria and whether disable coasts or holds before the demo.

## Confirmed analog mapping and proposed endpoint behavior

- Select any available accessible ADC input, but check the Nucleo schematic and existing pin use before choosing it. Do not select a pin solely because it appears unused in application code.
- Linear mapping: target_deg = clamp(filtered_input_volts / 3.0, 0, 1) * 360. Voltage is relative to MCU analog ground and must stay within the verified ADC electrical range; 3.0 V is command full scale, not ADC reference voltage. Calibrate ADC conversion/scaling against measured voltage and reference.
- Check sample settling/source impedance and choose sampling time accordingly. Log raw ADC, input voltage, mapped target and actual angle before motor operation.
- Analog command is an endpoint-bounded sweep coordinate: retain 0 and 360 as distinct command endpoints even though both have the same modulo encoder orientation. A rising input across the full range should command one bounded revolution, and a falling input should return through the same range. Do not normalize each analog sample to [0,360) and take the shortest path, which could produce a reversal near the wrap.
- Keep an unwrapped trajectory/feedback coordinate for this bounded travel, plus modulo angle for display and electrical commutation. Establish a documented zero/travel reference before arming; the encoder alone cannot distinguish the 0 and 360 endpoint after a reset. Reference recovery and physical clearance must be verified.
- Filter analog noise and limit trajectory velocity/acceleration; prevent endpoint noise from causing repeated wrap decisions. Confirm endpoint handling with shaft motion and telemetry.
- Zero volts means the zero-degree endpoint, not disable. A disconnected wire pulled to zero is indistinguishable from a valid 0 V command with a single full-range ADC signal. Require an independent enable and define disconnect handling explicitly; do not claim automatic analog disconnection detection unless a separate signal/mechanism is provided.

## Firmware architecture plan

See [FIRMWARE_ARCHITECTURE.md](FIRMWARE_ARCHITECTURE.md) for proposed source layout, module interfaces/ownership, bare-metal scheduling, command/state rules, board separation, DMA policy and implementation slices. It is a design plan, not implemented firmware.

## Proposed firmware structure

- Keep board-specific pins, clocks, polarity, driver configuration, ADC scaling, PHY and memory setup separate for Nucleo and custom PCB targets. Preserve the current custom-board IOC; use the new sibling Senior_Design_DEV_FW project and correct its board-specific settings per DEV_PROJECT_REVIEW.md.
- Share motor-control, encoder, limits/fault logic, command validation and telemetry across targets.
- Command path: analog or Ethernet -> validated setpoint with units and timestamp -> limits/slew -> motor loop -> PWM. Select one active source explicitly. Changing source requires a defined stop/arming transition.
- State machine: BOOT -> DISARMED -> CALIBRATING -> READY -> RUNNING; faults enter FAULT with output disabled. Calibration may energize the motor and requires explicit bench enable. Reset, watchdog, initialization failure and fault clearing must not auto-restart motion.
- Run the motor loop from deterministic timer/ADC events. Service networking and logging outside that loop; bound I2C transactions and reject stale samples.
- Candidate PWM starting point: around 20 kHz, subject to driver mode, gate timing and sensing review. Set encoder/velocity/current-loop rates from measured latency and CPU budget rather than forcing every task to the PWM rate.
- If six PWM inputs match the final PCB, use them for representative validation. Consider three PWM only as a documented bring-up choice after checking the driver's mode/input truth table. Determine dead time from both MCU and driver behavior.
- Demo Ethernet is implemented as strict ASCII G1/raw UDP, explicit millidegrees/session/sequence and 500 ms lease. ARM/CALIBRATE/ZERO stay on VCP. See the DEV firmware guide. Final-system protocol remains open; reconnect cannot energize this demo automatically.

## Bench setup and protections to implement

- Separate motor-bus power from the analog command source. Confirm supply earth/isolation and grounding. Never apply motor-bus voltage to an ADC pin.
- Prefer a 3.3 V referenced potentiometer or conditioned voltage input for initial analog checks. If using a separate supply, review divider/protection/filtering, ADC reference, source impedance and settling time; verify voltage with a meter before connection.
- Define voltage range, disconnect handling, filtering, angle mapping, permitted travel and target slew rate. For position mode, zero volts may mean the minimum angle; it must not implicitly mean stop/disable. A disconnected input pulled to zero could command an unintended move, so choose a detectable valid voltage window or an independent input-valid/enable mechanism. Analog alone must not arm the motor; require a separate enable.
- Build a secured motor fixture, strain relief and accessible power/enable cutoff. Keep switching power wiring short and separate from encoder wiring.
- Confirm board rail ownership to avoid backfeeding Nucleo and EVM regulators. Check continuity and polarity before power.
- Set an initial bus voltage within the verified board/motor range, bench current limit, phase-current limit, duty/voltage limit, speed limit and test duration. Bench supply current limit does not replace phase-current protection.
- Verify current shunts/gain/offset/polarity and bus-divider scaling; schedule current sampling in valid PWM windows. Do not rely on defaults.
- Account for regenerative bus rise during deceleration; confirm whether supply can sink energy or add a suitable clamp/load. Choose ramp and stop behavior accordingly.
- Use hardware shutdown where supported and a latched software fault path. Check enable/fault pull resistors and gate-off behavior with MCU unpowered/reset. Distinguish controlled stop from immediate fault disable.
- Use an appropriate differential/isolated measurement method for phase/gate nodes. A grounded scope probe on a switching node can short the power stage.
- Plan non-destructive tests for encoder disconnect/stale samples, analog unplug, driver fault, reset, watchdog and network loss. Do not create deliberate phase shorts to test protection.

## Additional validation beyond visible rotation

- Encoder direction, electrical offset, pole-pair verification, wrap-around and startup alignment; avoid permanent AS5600 OTP programming during experimentation.
- Setpoint saturation, integrator anti-windup, ramps and reversals; define coast/brake/hold requirements.
- Feedback dropout, fault latch/clear and command-source changes; motor never restarts merely because a cable reconnects.
- Loop execution time and worst-case jitter while Ethernet and telemetry are busy; I2C recovery must not block shutdown.
- Longer run with measured current, bus voltage and temperature; external temperature measurement if hardware lacks sensors.
- If multiple motors are required, repeat with separate driver stages and encoder address isolation, then test simultaneous control, timer/ADC scheduling and aggregate network load.
- Dev boards validate shared algorithms and software behavior. Custom PCB power integrity, gate drive, grounding, thermal behavior, sense calibration, clock/PHY and EMC still require their own bring-up.

## Questions awaiting user answers

1. Board part numbers confirmed: NUCLEO-H753ZI and BOOSTXL-DRV8304H. Record physical board revisions/jumper settings on arrival.
2. FIT1036 confirmed. Verify supplied encoder connector order and received EVM strap population; initial demo uses one axis.
3. Analog mapping confirmed: 0–3 V -> 0–360 degrees on an available ADC. Choose/verify pin and input conditioning, mechanical zero/travel reference, and endpoint trajectory behavior.
4. Typical bench supplies, scopes and multimeters are available. Record actual supply voltage/current ratings and sink capability, suitable differential probe availability, and full-turn mechanical/cable clearance before motor-power tests.
5. Custom PCB MCU confirmed STM32H743ZIT6; dev MCU STM32H753ZIT6. PCB has Ethernet PHY, CAN transceiver and two ESCs. Schematic parts and maps are recorded in HARDWARE_REFERENCE.md. PWM mode, current timing/protection and bench validation remain open.
6. Existing motor-control code/library preferences; axis travel, startup/homing and calibration persistence; camera mass/balance and loaded holding requirements.
7. Laptop is the command peer; final Ethernet or CAN selection, existing protocol, required update rate/latency, network topology and host-tool language remain open. Angle targets versus raw image offsets is undecided. Camera-on-gimbal and entire-object containment in central 80% are confirmed; specify ROI dimensions/crop and numeric tracking acceptance thresholds.
8. Parts expected Oct 8 night; demo mid next week (approximately Oct 14–15). Exact time remains open. Analog first, Ethernet after analog succeeds. Adjust proposed position error/settling/timeout criteria based on bench results.

## Decisions and provisional defaults

Current implementation settings are documented in DEV_FW/docs/FIRMWARE_GUIDE.md; historical proposals below are superseded where the implementation record says so.

| Date | Item | Status |
| --- | --- | --- |
| 2026-10-07 | Initial scope: dev boards and one motor; analog first, then Ethernet | Latest user clarification; supersedes communications-first ordering |
| 2026-10-07 | NUCLEO-H753ZI + BOOSTXL-DRV8304H | User confirmed; driver configuration uses hardware straps, not SPI registers |
| 2026-10-07 | Analog 0–3 V commands 0–360 degrees using an available ADC | User confirmed first bring-up milestone |
| 2026-10-07 | 0–360 degree demo travel; typical bench instruments available | User confirmed |
| 2026-10-07 | Parts expected Oct 8 night; demo mid next week | User estimate; exact time pending |
| 2026-10-07 | Unloaded, bounded angular travel, rate-limited moves and separate enable | Proposal; voltage/angle mapping and hold/startup behavior open |
| 2026-10-07 | Shared motor loop across board targets and command sources | Proposed architecture |
| 2026-10-07 | Camera/laptop/PCB/two motors; two-axis object tracking within 80% ROI | User confirmed entire object inside central 80%, with camera on gimbal; numeric tracking tolerance remains open |
| 2026-10-07 | Final laptop link may be Ethernet or CAN | User confirmed options; transport selection open |
| 2026-10-07 | Wiring, numeric operating limits, protocol and motor algorithm | Open; do not treat as settled |

## Readiness checklist

- [ ] Exact hardware and final-board differences confirmed
- [ ] Analog and subsequent Ethernet position-demo behavior and acceptance limits agreed
- [ ] Pin/harness table and power ownership checked
- [x] Nucleo build/clock/debug baseline prepared (software builds; measured hardware timing pending)
- [ ] Encoder and command-input acquisition validated
- [ ] PWM, shutdown and sense scaling verified
- [ ] First motion and analog 0–3 V position milestone logged
- [ ] Feedback/calibration and required control modes validated
- [ ] Ethernet-only baseline and loss-of-command handling validated
- [ ] Ethernet motion demo and combined timing checks completed
- [ ] Two-axis loaded position and synthetic tracking tests completed
- [ ] Camera tracking, ROI containment and dropout/reacquisition measured
- [ ] Custom-board transfer checklist repeated

## Bench session record (copy for each run)

- Date / operator / stage:
- Firmware revision or source snapshot / target / toolchain:
- Board part numbers, revisions and jumper/strap settings:
- Wiring map revision / motor ID / encoder settings and calibration:
- Supply voltage and current limit / measured bus / configured phase-current, duty and speed limits:
- Command source, mapping, setpoints and timeout:
- PWM, encoder and control-loop rates / measured timing:
- Procedure, expected result and numeric acceptance criteria:
- Observed result / plots, serial logs and scope capture paths:
- Fault/stop behavior and measured response time:
- Pass / fail / blocked; issue and next action:

## Change log

- 2026-10-07: Created plan from user intent, current project inspection, and vendor documentation. No hardware tests or firmware changes performed.
- 2026-10-07: User confirmed analog position control; changed demo prerequisites, acceptance criteria and input-loss requirements accordingly.

- 2026-10-07: User described two-axis vision-tracking system and Ethernet/CAN options; added control ownership, ROI definition questions, timing, loaded-gimbal and tracking acceptance tests.
- 2026-10-07: User confirmed camera-on-gimbal and entire-object containment within central 80%; command semantics remain undecided.
- 2026-10-07: User narrowed initial demo to dev boards and motor, demonstrating motor control via communications. Analog made optional; camera/two-axis tracking explicitly deferred; added minimum comms demo and pass criteria.
- 2026-10-07: User confirmed 0–360 degree travel, normal bench instrument access, expected parts Oct 8 night and demo next week; added schedule, single-turn angle convention and proposed numeric acceptance criteria.
- 2026-10-07: User confirmed H753ZIT6 dev MCU versus H743ZIT6 custom MCU, PCB PHY/CAN/two ESCs, mid-next-week deadline, and analog 0–3 V -> 0–360 degrees as first milestone followed by Ethernet. Added explicit bounded endpoint trajectory and independent-enable requirements.
- 2026-10-07: User confirmed low-side 14 mΩ shunts, 3.3 V reference, proposed gain 20 and expected current around 4 A; recorded conditional ADC headroom calculation in motor-control design notes. Schematics to be placed in Datasheets_UserManuals.
- 2026-10-07: Added firmware architecture document for analog-first demo and migration to Ethernet, measured-current FOC and H743/two-axis hardware.

## Schematic and new dev-project review — 2026-10-07

See [HARDWARE_REFERENCE.md](HARDWARE_REFERENCE.md) and [DEV_PROJECT_REVIEW.md](DEV_PROJECT_REVIEW.md). At the Oct 7 review the sibling H753 project had consistent target/startup/linker metadata but no motor application. Its clock source and Ethernet TX mapping need Nucleo-specific correction; configure deliberate PWM timing and fault inhibition before motion. Proposed command ADC is PA3 / A0 / CN9 pin 1. Custom encoder connector J5 lacks power/GND, ENABLE pins have pull-ups, and VM capacitors C44 are shown as 25 V on nominal 24 V. User confirms U6, R7 and buffer bypass jumpers are initially unpopulated. No firmware, build or hardware tests were performed in this review.

## Vendor-research implementation baseline — 2026-10-07

Active project: Senior_Design_DEV_FW (H753). New shared modules live there; H743 later links the shared code with its own board bindings. [ARCHITECTURE_RESEARCH.md](ARCHITECTURE_RESEARCH.md) records the primary-source review. Retain encoder-oriented voltage control for the bounded unloaded demonstration and current FOC for later loaded-gimbal validation.

Use bounded periodic asynchronous encoder reads with measured filter/transport/duty-hold lag and filtered velocity; slow the trajectory if lag requires it. Configure and scope the proposed PE6 TIM1_BKIN2 fault path. Ethernet uses foreground LwIP NO_SYS/raw UDP, with command expiry checked locally independently of stack servicing. Record execution maxima under network traffic. Full 360-degree timing must respect validated speed/acceleration; the earlier 2-second settling proposal does not guarantee a full-turn move in 2 seconds. Stage numbers above are now sequential.

## Implemented baseline — 2026-10-08

User confirmed FIT1036, 24 V bus and ST-LINK VCP. DEV_FW now contains the one-axis voltage servo, 0–3 V PA3 target, calibration/ZERO/ARM/stop/fault supervision, six PWM/break, asynchronous AS5600/ADC, watchdog, bounded console, LwIP UDP session/sequence/heartbeat and laptop client. Clocks use HSI and Ethernet uses the Nucleo RMII pins. The old IOC is archived; board.c owns this demo configuration. Detailed harness/straps and software-versus-bench evidence are in the linked DEV guide.

Defaults are 1 V alignment, ±2 V q effort, 0.4 rad/s and 0.4 rad/s² trajectory. These are configurable bring-up values, not measured phase-current limits. Full-turn reference duration is about 17 s plus settling. Current regulation/CAN/two axes/vision remain deferred. All physical acceptance boxes stay unchecked until measured.

## Open-loop milestone — 2026-10-12

User requested staged validation with selectable open/closed loops and a separate open-loop firmware version. OPEN_LOOP maps PA3 0–3 V to a positive speed command; 0 V is zero/coast, 3 V is a provisional configurable 30 RPM ceiling. No encoder is required, no mechanical travel limit/stall detection is claimed, and no current regulation is added. Mode selection is inhibited-only and clears prior calibration/commands. The existing closed-loop position demo is retained.

Revised first-motion sequence: logic/ADC -> winding-disconnected PWM and fault shutdown -> unloaded open-loop alignment/spin/ramp/coast -> encoder/pull-up/health validation -> mode change/new calibration/ZERO -> bounded analog position -> Ethernet position. Hardware evidence is still pending. [Open-loop guide and correction table](../../Senior_Design_DEV_FW/docs/OPEN_LOOP_GUIDE.md) records assumptions and numeric defaults.

## One-line configuration and staged switching checks — 2026-10-13

Firmware v0.2.1 normal/native builds use the single `DEMO_DEFAULT_OPEN_LOOP` header line: 1=open-loop speed, 0=closed-loop position. Current default is 1; no automatic arm is added. Explicit CLI profile overrides are optional and stored separately. MCU PWM validation precedes connecting the gate driver; powered driver gate/source/non-overlap/shutdown checks precede connecting the motor. [Bench switching guide](../../Senior_Design_DEV_FW/docs/BENCH_SWITCHING_VALIDATION.md) explains the shared 20 kHz complementary carrier, nominal 500 ns deadtime, 120° duty envelopes, fixed-angle alignment, gradual electrical rotation and probe references. Safe complementary switching alone does not prove rotor synchronization; record loaded waveforms/current/RPM and hardware assumptions.
