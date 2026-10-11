# Firmware architecture research and implementation decisions

Reviewed: 2026-10-07 (America/Chicago). Scope: single-axis H753 dev demo followed by two-axis H743 gimbal.
Active firmware target: ../Senior_Design_DEV_FW, STM32H753ZIT6.
Canonical planning notes remain here; code-specific operational/validation guides live with DEV_FW.

## Assessment

The proposed layered firmware and cascaded position/velocity control are appropriate for this application. Keep the local servo on the MCU and vision/tracking on the laptop. Keep analog and Ethernet as command adapters into the same bounded trajectory/supervisor interface.

Encoder-oriented sinusoidal voltage control is a reasonable first unloaded position demonstration; measured-current FOC remains the recommended loaded-gimbal target. The voltage demonstration does not validate regulated torque, two-axis ADC scheduling or loaded tracking. This is an engineering assessment against vendor reference practice, not a claim of formal functional-safety or software-standard certification.

The important revisions are explicit sensor timing, a timer-break shutdown path, raw-API networking, applied-output anti-windup and a real two-axis ADC resource plan. The exact rates and performance still require bench evidence.

## Evidence and applicability

| Primary reference | Relevant evidence | Application to this project |
| --- | --- | --- |
| [TI SPRABZ0A, sensored PMSM FOC](https://www.ti.com/lit/an/sprabz0a/sprabz0a.pdf), sections 9.1–9.15 | Incremental PWM/feedback/current/speed/position validation; electrical alignment; inner-loop tuning before position | Keep staged bring-up. TI's hardware and software are C2000-specific; its bus voltages, gains and drivers are not STM32 settings |
| [ST motor-control ecosystem](https://www.st.com/content/st_com/en/ecosystems/stm32-motor-control-ecosystem.html), performance and compatibility tables | Example separates 20 kHz PWM, 10 kHz FOC and 1 kHz speed regulation. Lists H7 single FOC; dual FOC lists F3/F4/G4 | Multi-rate scheduling is established practice. This is not a benchmark of our H753 code or proof of H743 dual-drive SDK support |
| [ST UM1052](https://www.st.com/resource/en/user_manual/um1052-stm32f-pmsm-singledual-foc-sdk-v43-stmicroelectronics.pdf), sections 4.12 and 5 | Voltage-vector saturation and PWM-dependent low-side sampling windows | Limit modulation for valid current samples. This older STM32F manual is used for control principles, not current H7 package support |
| [DFRobot FIT1036 position example](https://wiki.dfrobot.com/fit1036/docs/24648) | AS5600, 11 pole pairs, angle P and velocity PI with bounded voltage demand | Direct manufacturer precedent for the motor-family demo. Verify actual SKU/pole pairs; do not copy its gains, voltage or speed limits. The example is Arduino/3-PWM, not our HAL/6-input driver port |
| [ams AS5600 datasheet](https://look.ams-osram.com/m/7059eac7531a86fd/original/AS5600-DS000365.pdf), timing characteristics | Configurable filter settling: 2.2, 1.1, 0.55 or 0.286 ms | Poll rate alone does not establish rotor-angle freshness. Filter settling is not a pure transport delay; measure dynamic lag |
| [ST AN4277, break overview](https://www.st.com/content/st_com/en/technical-documents/an4277-how-to-use-pwm-shutdown-for-motor-control-and-digital-power-conversion-on-stm32-mcus-stmicroelectronics_asset/chunk-001-topic-an4277-break-function-overview.html) | Advanced-timer break forces PWM into configured inactive/safe states | Use hardware break where routed; verify resulting gate states, polarity and restart latch |
| [H743 datasheet](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf), pin definitions; [H753 datasheet](https://www.st.com/resource/en/datasheet/stm32h753zi.pdf), pin definitions | PE6 supports TIM1_BKIN2. PB1 supports ADC1/2 channel 5, PF11 ADC1 channel 2, PC0 ADC1/2/3 channel 10, PA3 ADC1/2 channel 15 | Shared custom nFAULT already reaches a TIM1 break-capable pin. Candidate ADC allocation is possible, but must be scheduled/validated |
| [ST RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf), section 25.4.32 | ADC1/2 dual injected simultaneous conversion, with matched sequences/sampling times | Candidate current acquisition mechanism; handle axis context, trigger collisions and completion explicitly |
| [LwIP mainloop mode](https://www.nongnu.org/lwip/2_1_x/group__lwip__nosys.html), [NO_SYS API restrictions](https://www.nongnu.org/lwip/2_0_x/group__lwip__opts__nosys.html) | Single-context stack servicing, packet input in mainloop, periodic timeouts, callback raw API without OS | Use raw UDP callbacks, not sockets/netconn, for the proposed bare-metal demo |
| [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf), sections 3–4; [CubeH7 Ethernet example](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Applications/LwIP/LwIP_HTTP_Server_Socket_RTOS/readme.txt) | CPU/DMA cache coherence and Ethernet-accessible SRAM requirements | Define linker/MPU/buffer ownership before networking; verify map and receive/transmit behavior |

References checked as of the review date. ST's current product page advertises [MCSDK 6.4.2](https://www.st.com/en/embedded-software/x-cube-mcsdk.html). An older ST wiki release-note page describes early v6 limitations and must not be used to conclude that current MCSDK has no H7 support.

## Working project and shared-source ownership

Use Senior_Design_DEV_FW for all initial builds, flashes and generated H753 settings. Preserve Senior_Design_FW as the H743 hardware target.

New App/, Control/, Devices/, Platform/, Boards/ and Comms/ source modules should initially live in the DEV_FW project. Core/ and Drivers/ remain generated/vendor files. Use USER CODE hooks to call the application. When porting to H743, link shared source directories into its project with relative paths; select only the pcb_h743 board port. Do not copy and then maintain two divergent controller implementations.

Existing hardware/demo/research documentation remains canonical under Senior_Design_FW/docs; the dev-project README points here. No application source, linked resources or IOC changes are implemented by this research update.

Recommended demo profile: one axis, voltage effort, analog command, serial diagnostics; Ethernet compiled/enabled in the following milestone. Do not initialize unused SPI, CAN or a second actuator simply because they were copied from the custom IOC.

## Demo control path

Analog voltage or Ethernet angle -> command validation/source ownership -> bounded trajectory -> position P -> velocity PI -> signed q voltage -> encoder electrical angle/inverse transform -> sinusoidal modulation -> TIM1.

For the later current profile, replace the voltage-effort stage with bounded q-current reference and d/q current PI. The current-sample completion path becomes the sole duty writer. Position and velocity loops remain slower tasks.

Keep P position / PI velocity as the initial choice. There is no rule that every loop needs integral or derivative. Account for static load in the velocity integrator, with saturation handling; later test gravity feedforward or a different outer-loop design only when the loaded mechanics justify it.

Anti-windup must account for the effort actually applied after voltage-vector, bus, duty-window and protection limits. A PI can wind up even if its own nominal output limit is respected but a downstream stage clips more tightly. Reset integrators on disarm/fault/reference changes; start active control from a coherent measured-position/zero-speed trajectory.

Full-turn mechanical endpoint semantics remain bounded/unwrapped [0, 2pi]; do not wrap the position error onto the shortest circular path, which would make a requested full turn collapse to zero. Electrical angle is modulo 2pi. Mechanical zero and electrical alignment remain separate calibrations.

## Sensor and scheduling revisions

Initial targets: 20 kHz center-aligned PWM, 1 kHz voltage-servo/commutation and encoder transactions, 250 Hz position/trajectory, 1 kHz velocity computation. These are execution rates, not promised loop bandwidths.

At 1 kHz and 12 bits, one encoder count per tick corresponds to about 1.53 rad/s of apparent velocity. Use a timestamped multi-sample slope or carefully tuned velocity filter; do not tune the PI around an unfiltered adjacent-sample derivative. Include filter lag in bandwidth assessment.

A standard two-byte register read with address write and repeated start uses about 45 SCL clocks: approximately 450 us at 100 kHz or 113 us at 400 kHz, before stretching/software overhead. This is a bus-budget calculation, not a measured transaction. Use nonblocking I2C IT initially; DMA is optional for such short reads. Validate any faster I2C setting against the sensor, pull-ups and harness.

The periodic scheduler should launch a bounded nonblocking encoder transaction when the bus is idle, so a long foreground network call does not control acquisition timing. A minimal callback publishes a complete raw sample and start/completion times. Bus recovery and register configuration run disarmed in foreground. Never wait for completion in a control ISR; never launch overlapping reads or replay catch-up bursts.

Record sensor filter/power/hysteresis configuration and status. Read magnet status periodically as a distinct health sample with its own maximum age, rather than silently treating an old status as current forever. A valid I2C completion timestamp is not the instant at which the physical angle was measured. Sensor filtering, wire transaction, software scheduling and duty-hold time all affect commutation lag.

Engineering speed bound: electrical_lag_rad approximately pole_pairs * mechanical_speed_rad_s * effective_delay_s. A provisional 5 electrical-degree budget and a measured effective 3 ms delay at 11 pole pairs would imply about 25 RPM. Neither 3 ms nor 5 degrees is a verified hardware parameter; this example explains how to choose an initial speed cap. Do not equate the datasheet settling number directly with pure delay.

If the measured lag/noise fails the required motion, first lower speed/gains and tune the sensor/filter/transaction configuration. For later faster current FOC, use a bounded, validated angle prediction between sensor samples or reconsider the encoder/interface. Running the same cached sample at 20 kHz does not create 20 kHz position information.

## Hardware shutdown revision

The previous review left PE6 alternate-function routing unresolved. Datasheets now confirm PE6 can be TIM1_BKIN2 on both H743 and H753.

For the single-axis demo, reserve PE6 as the proposed active-low driver-fault input to TIM1_BKIN2, subject to the EVM harness/board bridge check. Configure hardware output inhibition plus the break IRQ fault latch, no automatic output restart, and an explicit re-arm. Select break filtering based on measured noise/latency. Scope the actual MCU and gate outputs during fault/reset/debug tests.

On the custom PCB, shared nFAULT already connects to PE6, so this can inhibit TIM1 in hardware. The drawing does not show a direct nFAULT route to a TIM8 break pin. A TIM1 break handler should also inhibit TIM8 and both enables; this is software response for axis 1, not proof of simultaneous hardware break. Before two-axis operation, establish a verified second hardware shutdown route or substantiate the complete driver/MCU fault-response design and latency.

Fault response must be effective during a stalled foreground loop. Driver protection, timer break and software supervision have different roles; a watchdog is a recovery mechanism, not instantaneous short-circuit protection.

## Current-FOC resource plan for the custom board

Do not implement two axes by independently configuring the same ADC peripheral twice. Reserve a single current-acquisition scheduler with per-axis conversion contexts, trigger ownership and sample validity.

A candidate low-modulation two-phase allocation, from the schematic and H743 pin functions:

| Axis | ADC1 phase sample | ADC2 phase sample | Reconstruction |
| --- | --- | --- | --- |
| 0 | B: PF11 / INP2 | A: PB1 / INP5 | C = -A-B |
| 1 | A: PC0 / INP10 | B: PA3 / INP15 | C = -A-B |

This changes peripheral allocation, not PCB wiring. It is not a frozen acquisition design. Use deliberately limited modulation so both chosen low-side shunts have a valid sampling interval. If that interval disappears, reject/fault or reduce modulation; do not process stale/invalid current as valid. A later dynamic phase-selection strategy needs its own supported channel/timing matrix.

ADC1/2 can be a simultaneous injected pair. TIM1/TIM8 sampling slots must be staggered or otherwise explicitly scheduled, with each result tagged to the correct axis and PWM period. Determine blanking, amplifier settling, acquisition/conversion time and next-duty latch deadline from scope measurements and the actual ADC clock.

Start with one axis at a candidate 10 kHz current rate with 20 kHz PWM; enable the second only after proving noncolliding slots and bounded total execution. ADC3 is a candidate for slow phase-voltage/temperature diagnostics. Bus sense/pots may need low-priority regular groups on ADC1/2; schedule them so they cannot invalidate a current deadline. No DMA/oversampling choice should be assumed to solve current-window validity.

Current-FOC saturation uses the measured bus and the permitted modulation window; maintain anti-windup and per-axis gain/offset calibration. Establish zero current before excitation, validate A/B/C polarity and d/q transformation with gentle known tests, then tune current, speed and position in that order.

## Ethernet and memory implementation

Use NO_SYS=1 and the raw callback UDP API for the proposed bench protocol. All LwIP calls execute in foreground; the Ethernet ISR handles/flags driver events only. Call sys_check_timeouts regularly and bound receive processing before returning to acquisition/other foreground services. Callbacks validate/copy a small command into a mailbox; they do not run controllers or change PWM.

Application command expiry runs against a local monotonic clock in the control/supervisor path, independently of successful network service. Packet arrival, status traffic and link state are not equivalent to a fresh authorized motion command.

Prefer explicit noncacheable DMA-accessible SRAM regions for descriptors and demo network buffer pools, with correct MPU attributes/alignment and ownership. If cached buffers are later chosen, use documented cache maintenance and isolated cache-line boundaries. Check payload buffers as well as descriptors. Read the final linker map and verify TX/RX under load; disabling D-cache alone does not fix an inaccessible-memory address.

Keep small nonblocking serial and analog acquisition independent of the network stack. Retest control timing after Ethernet is enabled.

## SDK/library decision

Do not assume the MCU family entry implies a ready-made project for NUCLEO-H753ZI + BOOSTXL-DRV8304H + I2C AS5600. Neither the manufacturer Arduino example nor TI's C2000 example is a drop-in HAL backend.

Proceed with the small, explicitly bounded voltage-demo implementation in DEV_FW. Use vendor examples for formulas, validation sequence and architecture; do not paste whole board-specific projects into main.c. Before investing in full custom current FOC, evaluate a pinned MCSDK release/generated H753 single-axis prototype against the actual driver, sensor API and ADC mappings. Compare integration cost with a small reusable custom current backend.

ST's compatibility matrix lists H7 single FOC, but not H7 dual FOC in its dual-drive rows. Two H743 axes are therefore a custom integration/validation task; do not promise generated dual-drive support. An RTOS can be introduced later if application services require it, while the motor loop remains ISR-driven; it is not needed to make this single-axis demo valid.

## Acceptance evidence needed

- Reproducible H753 build/flash with recorded package/clock/board configuration.
- Motor-disconnected scope proof of carrier, complementary states, fault inhibition and reset behavior.
- Meter-matched ADC input and timestamped encoder/filter/status/velocity data.
- Low-energy phase-order/direction/electrical-zero calibration and bounded signed-effort response.
- Analog position move/hold/error/settling within measured limits, including endpoint/reference behavior.
- Encoder stale/loss, fault, voltage limit and watchdog behavior.
- Ethernet loss/malformed/duplicate/reconnect tests plus control deadline measurements under traffic.
- For later current FOC: valid current windows, transformation signs, step response and per-axis scheduling proof.

A full 360-degree move requires at least 360 / allowed_speed_deg_per_s seconds before acceleration/deceleration. The earlier 2-second proposal applies only to selected rehearsed moves unless the validated speed/trajectory can actually meet a full-turn timing requirement.

No firmware build, SDK integration, bench validation or formal compliance audit was performed for this documentation update.

## Implementation choices — 2026-10-08

DEV_FW implements the vendor-backed voltage-first architecture with local P-position/PI-velocity, timed calibration, asynchronous raw AS5600/status reads, explicit TIM1 BKIN2 inhibition and raw UDP foreground servicing. D-cache is deliberately disabled and DMA storage explicitly linked in D2; current FOC and formal compliance remain outside validated scope.

Practical changes from the initial proposal: hand-owned BSP with original IOC archived; HSI-derived 200 MHz CPU to avoid uncertain ST-LINK MCO settings; all voltage servo/motion loops at 1 kHz initially; human-readable bounded ASCII G1 packets instead of fixed binary for bench captures; no network ARM/calibration/zero. TI wake/sleep nFAULT assertion is handled with hardware break always active and IRQ notification only during authorized drive. Initial 24 V, 1 V alignment, ±2 V effort and slow trajectory require bench validation. See [implemented architecture](FIRMWARE_ARCHITECTURE.md).
