# PCB bench procedure and bindings

2026-10-23. User confirms the PCB matches the nine-page schematic reviewed in `Senior_Design_FW/docs/HARDWARE_REFERENCE.md`; no changes were reported. User selected J3, center-stop bidirectional VR1, S1 ARM, S2 STOP, DRV8304S CSA gain 20 V/V and lowest gate-drive current. MOSFETs confirmed SiS890ADN-T1-GE3. Supply is 24 V, proposed **2 A bus current limit**; programmer is STLINK-V3MINI. Initial power checks reportedly look good; measured values are still to be recorded.

## Pin audit

The schematic's physical pages 3/5/6/7 were rechecked, including the inter-sheet nets. H743 pin functions were cross-checked against ST DS12110 and the baseline IOC. The following are the bindings implemented by this target:

| Signal | H743 pin | Detail / probe point |
| --- | --- | --- |
| J3 A high / low | PE9 / PE8 | TIM1_CH1 / CH1N AF1; TP17 / TP18 |
| J3 B high / low | PE11 / **PB0** | TIM1_CH2 / CH2N AF1; TP19 / TP20 |
| J3 C high / low | PE13 / PE12 | TIM1_CH3 / CH3N AF1; TP21 / TP22 |
| J3 ENABLE / CAL | PG4 / PG5 | Active high / held low; TP30 / TP29 |
| J3 chip select | PE5 | Active low, idle high |
| J3 enable / fault LEDs | PE3 / PE2 | Active high, MCU indicators; TP32 / TP31 |
| J4 ENABLE / CAL | PG7 / PG6 | Active high / held low; TP34 / TP33 |
| J4 chip select | PA9 | Active low, idle high |
| J4 enable / fault LEDs | PC10 / PC11 | Active high, selected-driver indicators |
| Shared nFAULT | PE6 | Active low, TIM1_BKIN2 AF1 |
| SPI2 clock / MISO / MOSI | PB10 / PC2_C / PC3_C | AF5; `_C` switches explicitly closed |
| **VR1 / POT1** | **PA4, ADC1_INP18** | **TP12, motor command**, first ADC rank |
| VR2 / POT0 | PA0, ADC1_INP16 | TP13, diagnostics only, second ADC rank |
| S1 ARM / S2 STOP | PD8 / PD9 | Pressed low; external 10 kOhm pull-ups |
| Blue / green / red LED | PD12 / PD11 / PD10 | LED1/2/3, active high via MOSFETs |

The phase test-point identities follow page 5, including its embedded net metadata. VR1 was traced through R84/TP12/POT1 to U1 pin 40 (PA4); VR2 through R85/TP13/POT0 to pin 34 (PA0). Reference-designator numbering and POT net numbering are reversed. Verify silkscreen/continuity before placing probes.

J4 PWM pins PC6/PA5 (A high/low), PC7/PB14 (B), PC8/PB15 (C) always remain GPIO outputs low; TIM8 is not initialized. PCB_TEST_AXIS=1 can wake J4 only in DRIVER_CHECK. The unselected ENABLE stays low and its nCS stays high; both CALs stay low. In DRIVER_CHECK, all 12 PWM pins are GPIO outputs low. PE6 is a GPIO input for the J4 driver check. Its TIM1 hardware-break route remains used for J3 motor operation; no equivalent TIM8 shutdown route has been established. Axis 1 PWM/motor stages are rejected at compile time.

## First flash with STLINK-V3MINI

Power off while making connections. Connect by **signal**, observing connector pin-1 orientation; a 14-pin STDC14 lead must not be assumed pin-for-pin with J6's 10 pins.

| STLINK-V3MINI STDC14 pin | Signal | PCB J6 pin |
| --- | --- | --- |
| 3 | T_VCC target-voltage sense | 1, board 3V3 |
| 4 | T_SWDIO | 2 |
| 6 | T_SWCLK | 4 |
| 12 | T_NRST | 10 |
| 5 / 7 | GND | 3 / 5 |
| 11 | GNDDetect | 9, GND |

T_VCC is an **input** to this probe; supply the PCB using its intended power input, not from this sense pin. J6 pins 6/7/8 are unused in the supplied schematic. Check actual cable/adapter continuity. USB powers the programmer.

Start with both motors disconnected and `PCB_STAGE PCB_IO_CHECK`. In CubeIDE select this H743 project, build Debug, then configure ST-LINK GDB server, SWD, Cortex-M7 access port, Connect under reset, initially 1000 kHz. Load `Debug/Senior_Design_PCB_BRINGUP.elf`; enable download/verification and run from reset. The actual H743 should identify correctly and report the target's 3.3 V rail. Do not use the old H753 ELF or the baseline generated H743 ELF.

The firmware uses internal HSI/PLL: nominal CPU 200 MHz, AHB 100 MHz, timer kernels 100 MHz. No HSE crystal frequency is guessed. It does not initialize Ethernet, CAN, I2C, UART or encoder interfaces.

## Stage 1: I/O check

- Blue LED1 blinks at 1 Hz (0.5 s on/off).
- Press S1: green LED2 turns on. Press S2: red LED3 turns on. Release: they turn off, unless a firmware fault holds red on. These are button mirrors only in this stage.
- Driver-enable/fault LEDs are off during normal IO check. Neither driver is woken. All 12 PWM inputs, both ENABLEs and both CALs must measure low after startup; both CS pins must measure high.
- In Live Expressions, watch `g_app_status.adc_raw[0]` while moving **VR1**, and `[1]` while moving VR2. Expect continuous change close to 0..65535, nominal center near 32768. Check TP12/TP13 with a meter. Confirm each pot changes only its own channel. Record actual endpoints/center and VREF+/VDDA versus pot supply.
- `adc_valid` should be true, `adc_age_ms` normally around 1 ms and below 20 ms. `ticks` increments about 1000/s; `max_control_us` must stay below 500 us. `driver_good` is not required while both drivers sleep.

The ADC uses two regular conversions with a long 387.5-cycle acquisition at nominal 12.5 MHz, interrupt delivery and a coherent pair timestamp. Board filters are 10 kOhm / 100 nF. I-cache is on; D-cache and DMA are unused.

## Stage 2: MCU PWM check

Set `PCB_TEST_AXIS 0` and `PCB_STAGE PCB_PWM_CHECK`, rebuild/flash. Motors remain disconnected. Verify PG4 and PG7 stay low in every state. Let go of both buttons, press S1, center VR1 if green is blinking. After 200 ms of valid neutral input, green is steady and gates are still off. Move VR1 slightly out of center. After the wake allowance, the six J3 MCU inputs show complementary 20 kHz PWM, nominal 500 ns MCU dead time; the duty envelope changes slowly with field rotation. Scope each high/low pair and all three phase identities. Returning to neutral ramps to zero and turns PWM off after coast; S2 disables immediately.

No driver is enabled in this stage. nFAULT is not used as a power-stage break in this MCU-only check. The physical STOP interrupt, ADC freshness, control timing, foreground freshness and watchdog remain active.

## Stage 3: driver configuration with all PWM off

Set `PCB_STAGE PCB_DRIVER_CHECK` and `PCB_TEST_AXIS 0` for J3 or `1` for J4, rebuild/flash, both motors disconnected. Release S1/S2, center VR1, press S1, then move VR1 slightly out of neutral. The selected ENABLE (PG4/J3 or PG7/J4) goes high; the other stays low. All 12 PWM inputs remain GPIO low. No serial command or encoder is involved.

After at least 2 ms awake, foreground SPI configures and verifies the driver. Mode 1, 16-bit MSB-first frames, nominal 781.25 kHz; nCS setup/hold at least 1 us and inactive spacing at least 2 us. Both selects are initialized high. Only the selected nCS is asserted: PE5 for J3 or PA9 for J4. The other select is held high before every transaction. Full wake/configuration allowance is 20 ms before alignment is permitted in the motor model. Read `g_driver_status` while running, using Live Expressions with outputs inhibited.

| Address | Expected data | Meaning |
| --- | --- | --- |
| 0 / 1 | 0 / 0 | No fault status detected at verification time |
| 2 | `0x080` | 6-input PWM; charge-pump/gate faults enabled; thermal warning reported |
| 3 | `0x300` | Unlocked config; HS gate current 15 mA source / 30 mA sink |
| 4 | `0x300` | LS same minimum currents; 4 us peak drive; CBC disabled |
| 5 | `0x300` | 400 ns driver dead time; latched OCP, all bridges shut down; VDS threshold 0.15 V |
| 6 | `0x280` | Gain **20 V/V**, VREF/2 midpoint, sense OCP enabled, 0.25 V threshold; CAL disabled |

COAST is set while configuration is written, then cleared with physical PWM still low. Every write is checked by a separate read; the complete profile and fault registers are rechecked every 100 ms while enabled. A failed frame/readback/status check latches a fault and inhibits the bridge. Each wake after a stop/coast repeats configuration. Stopping or faulting during a foreground SPI transfer cancels its permission; completion cannot re-enable the bridge. No automatic fault-clear bit or retry is used.

Minimum gate current was user-selected. For the confirmed SiS890ADN, published Qg(max)=29 nC at 10 V gives a rough 29 nC/15 mA = 1.93 us charging estimate, inside 4 us TDRIVE. Actual gate switching, ringing, losses and fault behavior must still be scoped. A 0.15 V VDS threshold is a conservative initial selection, not a calibrated current limit; false trips must be diagnosed rather than disabling protections.

Verify nFAULT high after wake and the driver supply rails. With PWM held off, gate-to-source voltages should remain off. S2 lowers both ENABLEs immediately; normal sleep/wake can pulse shared nFAULT. In this stage firmware does not set TIM1 MOE, so the bridge never receives PWM.

## Stage 4: motor run

Set `PCB_TEST_AXIS 0` and `PCB_STAGE PCB_MOTOR_RUN` only after IO, MCU PWM, driver readback, reset behavior and component checks pass. Axis 1 MOTOR_RUN is not implemented and is rejected at compile time. Power off, connect one unloaded FIT1036 to J3 A/B/C. Wire order determines the physical direction; establish labels at low speed. Start with the user's proposed 24 V / 2 A bus limit and monitor current. Current limiting must still allow the logic rail to remain stable; a supply foldback/reset can invalidate startup observations.

Release both buttons; center VR1; press/release S1. Green steady means armed. Moving VR1 out of neutral wakes/configures the driver, aligns the rotor for 2 s (1 V phase-amplitude target, ramped over 0.5 s), then ramps the rotating field at 10 RPM/s to the selected command. Run phase-voltage amplitude is 2 V with 2 V/s slew, using the configured nominal 24 V bus. PWM is center-aligned 20 kHz; duties are `0.5 + phase_voltage/24`, clamped to 5..95%. At 2 V amplitude that is about 41.7..58.3%. The sine references are separated by 120 electrical degrees; the carrier timers are synchronous.

| Nominal VR1 voltage | Command |
| --- | --- |
| 0 V | -100 RPM |
| 0.715 V | -50 RPM |
| 1.43..1.87 V | Neutral, 0 |
| 1.65 V | Neutral center |
| 2.585 V | +50 RPM |
| 3.3 V | +100 RPM |

These are **field-equivalent mechanical commands** using 11 pole pairs, not measured RPM. At 100 RPM command the electrical rotation is about 18.33 Hz. No encoder/current feedback is used. Pot conversion is normalized ADC travel, not a calibrated voltage measurement; the intended VREF/pot rail relation must be checked in Stage 1.

Zero volts commands full reverse after arming; it is not STOP. There is no pot wire-break/short detection in this build. Use S2 to inhibit the bridge.

An S1 request at an extreme waits with ENABLE/PWM off until the pot remains neutral for 200 ms. Leaving neutral or invalid/stale samples restarts that dwell. Booting with S1 held cannot arm: a stable release followed by stable press is required. S2 overrides ARM, immediately inhibits outputs through EXTI and is checked again before authorization; a press overlapping STOP must be released before rearming. STOP cancels waiting/align/run and does not perform a ramped stop. Faults stay latched until reset, which starts disabled again.

Returning the pot to neutral decelerates the commanded field at 10 RPM/s, then disables/coasts for 1 s and returns to armed idle. Reversal first ramps to zero, disables/coasts 1 s, then repeats wake/configuration/alignment before ramping the other way. The 1 s pause does not prove rotor standstill. Neutral does not provide powered position holding.

Use ground-referenced probes only with **every ground clip at board ground**. Gate-source for high side is GH minus SH, requiring a differential probe or two correctly rated ground-referenced channels plus math. Never ground a floating phase with a probe clip. Phase-to-ground voltages look like 0-to-bus PWM; examine line-to-line averages/envelopes for the rotating excitation.

## Protection boundaries and hardware checks

- PE6 nFAULT drives TIM1 hardware break, active low, automatic output re-enable disabled. Its ISR also lowers both ENABLEs and latches a software fault. Test this path with the motor disconnected. Never force a push-pull voltage onto nFAULT; it is a shared open-drain net.
- ADC age over 20 ms, foreground age over 100 ms, control interval outside 700..1500 us, or control work over 500 us inhibit operation. The nominal 1 s independent watchdog is fed only while the control interrupt remains fresh. These software checks are not a hardware safety certification.
- ENABLE has a schematic **pull-up**. Software takes it low early, but cannot hold it low while the MCU is in reset. Scope power-on, NRST, ST-LINK connect/reset, watchdog reset and debug behavior. MCU PWM peripherals can continue while halted; do not single-step an energized motor.
- The 14 mOhm shunts with gain 20 yield nominal 0.28 V/A and VREF/2 zero. The minimum 0.25 V shunt-OCP threshold corresponds to about **17.9 A**, not 4 A. VDS protection depends on MOSFET resistance/temperature. A 2 A DC bus limit does not guarantee a 2 A phase peak; check phase current, particularly stationary alignment. No calibrated operating-current or temperature supervisor is implemented in this open-loop build.
- C44_I_0/1 is 1 uF / 25 V between VCP and VM; C45 bypasses DVDD and C46 bypasses VREF. The earlier C44 full-bus warning and VCP-short diagnosis were tracing errors. User measured >100 MOhm between VCP/VM and confirmed axis-0 C44=1 uF, C50=22 nF. See the corrected charge-pump review and startup capture below; no capacitor addition or trace cut is indicated.
- Previous assembly intent was U6 backup regulator, R7 regulator-EN pull-down and buffer bypass resistors unpopulated. User confirmed the schematic matches and reported no changes. Check the physical population and scope 5 V, 3V3_D, 3V3_A, VREF+ and VCAP on first board.

## Record the bench result

| Item | Result |
| --- | --- |
| Board revision / serial, fitted C44 rating | Pending |
| 24V0_M / 5V0 / 3V3_D / 3V3_A / VREF+ / VCAP | Power reportedly good; measured numbers pending |
| VR1 raw min / center / max; VR2 isolation | Pending |
| S1/S2 polarity and LED identity | Pending |
| Both ENABLEs low at boot/reset; unused J4 low | Pending |
| J3 PWM carrier/dead time/pin identities | Pending |
| SPI readback / nFAULT / gate-source off state | Pending |
| STOP and hardware-break behavior | Pending |
| First spin: supply current / phase current / temperature / bus overshoot | Pending |

Primary references: [TI DRV8304 SLVSE39B](https://www.ti.com/lit/ds/symlink/drv8304.pdf), [ST H743 DS12110](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf), [Vishay SiS890ADN](https://www.vishay.com/docs/78019/sis890adn.pdf), [STLINK-V3MINI UM2502](https://www.st.com/resource/en/user_manual/um2502-stlinkv3mods-and-stlinkv3mini-mini-debuggersprogrammers-for-stm32-stmicroelectronics.pdf). Local schematic SHA-256: `2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144`.

## Driver fault snapshot follow-up - 2026-10-23

User completed flashing (after correcting connector orientation), LED/button/VR1 IO checks and the three MCU PWM checks. Only VR1 is populated. ENABLE-high observation was resolved by resuming execution past main; both ENABLEs then measured low. DRIVER_CHECK subsequently faulted with VM=23.7 V and registers [0x400,0,0x080,0x300,0x300,0x300,0x280]. Configuration writes/readbacks match, but 0x400 is the driver's summary FAULT. The original verifier returned before reading register 1, so that zero did not identify the detailed fault.

The corrected verifier captures all seven registers before judging status/readback values. Watch g_driver_status.read_mask: 0x7f (127) means the complete verification snapshot was read. If capture fails, the mask identifies successful reads; unread or leftover values are not proof of current hardware status. A summary FAULT still fails verification and inhibits the driver. Reflash the updated DRIVER_CHECK build, Resume, ARM at neutral and move VR1 out of neutral to collect reg[0]/reg[1]/read_mask. Keep motors disconnected. Actual fault cause is not yet established. 23.7 V lies within the specified VM range; this capture does not call for raising the supply.

Register 0 bit 10 is the summary; detailed VGS/charge-pump/sense/thermal-warning flags are in register 1. Read both before selecting a hardware diagnosis. A summary fault without a detailed bit in either register also needs investigation of the shared nFAULT line and its other driver; do not ignore the summary bit to permit operation. No fault bits, protection thresholds or wake timings were changed by this diagnostic correction. No fault-clear command was added.

## CPUV identified; charge-pump wiring review - 2026-10-23

The updated user snapshot is [0x400,0x040,0x080,0x300,0x300,0x300,0x280], failed_address=0, verified=false, driver_good=false and FAULT_DRIVER_CONFIG. Register 1 bit 6 identifies charge-pump undervoltage (CPUV). Configuration registers 2..6 match. read_mask was not supplied; previously measured VM=23.7 V. The driver-reported fault explains the failed verification, but the electrical cause remains unknown.

**Correction of the earlier schematic review:** the assistant mis-traced C44 and incorrectly reported VCP shorted to VM, a missing storage capacitor, and C44 exposed to the full bus. Those conclusions are withdrawn. User reports **greater than 100 MOhm between axis-0 VCP and VM**, with power disconnected, and confirms **C44_I_0 fitted as 1 uF and C50_I_0 fitted as 22 nF**. High DC resistance is consistent with a capacitor between these nodes; it does not establish capacitance, solder integrity or charge-pump operation. Axis 1 has not been measured.

Close inspection of the same rendered source shows these connections on both inverter pages:

| Component | Drawn connection | Value / rating in drawing |
| --- | --- | --- |
| C44_I_0/1 | Pad 1: VCP pin 3; pad 2: VM / 24V0_M | 1 uF / 25 V, charge-pump storage capacitor |
| C45_I_0/1 | DVDD pin 33 to GND | 1 uF / 25 V, internal-regulator bypass |
| C46_I_0/1 | VREF pin 24 / 3V3_A to GND | 0.1 uF / 16 V, reference bypass |
| C50_I_0/1 | CPH pin 2 to CPL pin 1 | 22 nF / 100 V, charge-pump flying capacitor |

The C44 symbol interrupts the vertical wire: its lower pad goes to VCP, not GND. C43 and C82 are the neighboring VM-to-GND capacitors. **No trace cutting or additional storage capacitor is indicated by this drawing or the reported resistance.** The previous 25 V C44 full-bus margin warning was based on the same tracing error and is withdrawn.

[TI DRV8304 SLVSE39B](https://www.ti.com/lit/ds/symlink/drv8304.pdf), Table 1 / section 7.3.1.3, specifies a 1 uF X5R/X7R storage capacitor rated at least 16 V between VCP and VM, plus a 22 nF VM-rated flying capacitor. Nominal awake VCP is VM + 10 V, so C44 sees about 10 V difference, not the full bus. Section 7.3.5.2 states that CPUV status can remain latched after voltage recovery. A startup capture is needed to distinguish inadequate boost from a transient or sequencing issue. Fitted dielectric, effective capacitance, soldering, startup waveforms and loading have not been verified. MCU source, protections and active DRIVER_CHECK selection are unchanged.

### Next measurement: charge-pump startup

Keep motors disconnected and PCB_STAGE=PCB_DRIVER_CHECK (all PWM off). With power removed, attach appropriately voltage-rated 10x probes: CH1 tip at TP30 / PG4 / axis-0 ENABLE; CH2 tip at C44_I_0 pad 1 / VCP. Put **both ground clips at board GND**, never on either C44 pad. If a third channel is available, probe C44 pad 2 / VM to GND and use CH2 minus CH3 for boost headroom.

Use a single acquisition triggered on ENABLE rising at about 1.5 V, initially 1 ms/div with pretrigger, VCP about 10 V/div and sufficient range for 34 V. Restore supplies, reset/Resume, center VR1, release/press S1, then move VR1 outside neutral to request wake. Record VCP rise and ENABLE duration; optionally repeat with nFAULT or DVDD. Nominal VCP is about 33.7 V to GND for the reported 23.7 V VM; DVDD should approach 3.3 V while awake. Actual startup timing is still a measurement.

board_service starts SPI configuration once HAL tick elapsed reaches 2 ms and inhibits ENABLE when verification fails. Consequently a post-fault DC measurement of low boost is not evidence that the pump failed while enabled. Do not halt execution during the capture. A correctly boosted VCP with a retained CPUV flag calls for examining fault timing/status clearing; inadequate boost calls for checking local VM, C44/C50 connections and effective values, soldering and VCP loading. Neither result justifies bypassing CPUV protection.

### Inspected source detail

Unmodified rendered source crops: [axis 0, PDF page 6](images/charge-pump-page-6.png), [axis 1, PDF page 7](images/charge-pump-page-7.png). The C44 capacitor plates separate its VM and VCP wires.

## Temporary CPUV-history bench option - 2026-10-23

The user chose a small software exception for motor-disconnected phase-output checks instead of the proposed separate five-second diagnostic. No new mode, observation timer or automatic CLR_FLT operation was added.

In Core/Inc/pcb_config.h, PCB_BENCH_ALLOW_CPUV_HISTORY=1 permits verification to accept **only register 1 exactly 0x040 (CPUV), with register 0 either zero or exactly 0x400 (summary FAULT)**. Registers 2..6 must still match the original profile. Every other status bit, summary-only fault, configuration mismatch and SPI error remains a failure. The profile remains [0,0,0x080,0x300,0x300,0x300,0x280]: DIS_CPUV is not set, and no hardware protection or threshold is relaxed.

g_driver_status.cpuv_history_accepted exposes this specific exception. verified=true means readback passed the selected policy, including this optional history exception; it is not a voltage measurement. Raw fault registers remain visible. Live nFAULT HIGH remains necessary for driver_good and for enabling PWM. If CPUV is still electrically present, the driver protects the bridge and the application still faults when it cannot obtain driver_good after wake. Red can therefore change from FAULT_DRIVER_CONFIG to FAULT_DRIVER; this is not evidence that the charge pump recovered. The exception cannot force an unhealthy driver to switch.

The active Senior_Design_PCB_BRINGUP CubeIDE copy is set to **PCB_STAGE PCB_MOTOR_RUN** and **PCB_BENCH_ALLOW_CPUV_HISTORY 1** for this test. The repository/source archive defaults remain IO_CHECK and exception=0. This bench image can command PWM after arming; keep the motor disconnected for phase measurements. J4 remains disabled.

Procedure: power off before attaching appropriately rated 10x probes; all scope grounds go to board GND. Probe J3 phase A/B/C to GND, not GH-to-GND as a substitute for phase voltage. Build/flash the active Debug ELF and Resume. Release both buttons, center VR1 (nominal 1.43..1.87 V), press/release S1, then move VR1 outside neutral. The normal wake/alignment/ramp sequence remains. Phase-to-GND should show roughly 0-to-VM carrier pulses (about 24 V), whereas VCP is nominally VM+10 V. Pulse amplitude with an unloaded bridge alone does not validate loaded MOSFET operation or phase current. S2 immediately inhibits outputs. Capture driver_good, nFAULT, the two raw status registers and cpuv_history_accepted with any switching result.

There is no new five-second timeout: the existing neutral/S2/fault controls end excitation. Restore PCB_BENCH_ALLOW_CPUV_HISTORY=0 before motor-connected testing; that restores strict software status verification. No physical switching, voltage or motor result has been established by the agent.

Software checks cover the exception both off/on, every other status bit combined with CPUV, summary-only rejection, profile/transport failures, and the unchanged live-driver-good requirement at wake. See cpuv_bench_evidence.json for the build/test record. [TI SLVSE39B](https://www.ti.com/lit/ds/symlink/drv8304.pdf), section 7.3.5.2, documents automatic electrical recovery and retained CPUV status; this option does not prove a captured CPUV flag was transient.

## ENABLE and nFAULT timing follow-up - 2026-10-23

User's post-exception snapshot: verified=true, cpuv_history_accepted=true, driver_good=false, FAULT_DRIVER, and unchanged registers [0x400,0x040,0x080,0x300,0x300,0x300,0x280]. After fault, nFAULT was measured high with ENABLE low, so that observation describes shutdown, not recovery while awake. Latest scope report: ENABLE high 1 ms, low 7 ms, high 20 ms, then low; nFAULT exactly opposite throughout, including before/after. The initial pulse's cause is unestablished. The final 20 ms aligns with the application's wake/readiness deadline. If actual DC levels and non-inverted traces confirm nFAULT low for that whole ENABLE-high interval, it exceeds TI's normal <=1 ms wake interval and supports an active fault during wake. SPI CPUV status makes the selected charge-pump circuit the leading investigation; the shared fault line and actual VCP waveform still need verification.

Read-only firmware audit: PE6 is first an input, then TIM1 break-input alternate function; application GPIO writes do not drive PE6. The MCU does not intentionally generate an inverse of ENABLE on nFAULT. The bench exception accepts stored CPUV only; driver_good still requires driver_ready AND PE6 high. No PWM is permitted during the wake interval. Thus an asserted nFAULT at the deadline explains FAULT_DRIVER. No additional control/protection change was made.

Next measurement: retain the ENABLE trigger at TP30, move the other probe from nFAULT to C44_I_0 pad 1 (VCP), both grounds at board GND, and capture the same 20 ms wake interval after reset/Resume and the normal S1/VR1 command. Compare VCP to local VM: nominal boost about 10 V (approximately 33.7 V to GND for VM=23.7 V). Verify scope channel Invert is off and the measured nFAULT point is the shared fault net / driver pin 25 rather than an LED control signal. No particular assembly defect has been established; no phase-output or charge-pump-voltage capture has yet been supplied.

## J4 driver-only comparison - 2026-10-23

User authorized switching to the other driver for comparison. The active CubeIDE project is now **PCB_TEST_AXIS=1 (J4), PCB_STAGE=PCB_DRIVER_CHECK, PCB_BENCH_ALLOW_CPUV_HISTORY=1**. The CPUV-history option is unchanged from the latest J3 bench test: it accepts only stored CPUV status and does not disable the driver protection or bypass live nFAULT. Repository/source archive defaults remain **axis 0 / IO_CHECK / history exception=0**.

Only J4 ENABLE PG7 and nCS PA9 are selected. J3 ENABLE PG4 is forced low on every enable update, and nCS PE5 is held high before each frame. Disabling resets both ENABLEs. Both CAL pins and all 12 PWM pins remain low. J4 indicators PC10 (enable) and PC11 (fault) now follow the selected driver; the unselected driver's fault indicator stays off. Main LEDs and S1/S2/VR1 controls are unchanged. The actual GPIO routing helpers have simulated-BSRR tests for both axes.

Flash the active Debug ELF, Resume and confirm g_app_status.test_axis=1, stage=2. With both motors disconnected, center VR1 (nominal 1.43..1.87 V), release the buttons, press/release S1, wait for neutral qualification, then move VR1 outside center. **Probe ENABLE at TP34 / PG7, not the previous TP30.** TP30 / PG4 should stay low. Record g_driver_status.reg[0..6], verified, read_mask, cpuv_history_accepted, and g_app_status.driver_good/fault. S2 disables both drivers immediately; a latched fault still requires MCU reset.

For J4 measurements use C44_I_1 pad 1 / VCP, C44_I_1 pad 2 / VM, and C45_I_1 pad 2 / DVDD, each to board GND. Use DC coupling and appropriately rated probes; scope ground clips stay at board GND, never either C44 pad. While awake, nominal VCP is about VM+10 V and DVDD about 3.3 V. Do not infer sleep-state VCP from the awake specification. nFAULT is shared with J3, so the pin alone does not identify which IC pulled it low; the selected driver's SPI status is important.

If J4 remains healthy after the 20 ms wake check, ENABLE can stay high for measurement while VR1 is outside neutral. If its fault remains active, the same existing wake deadline still inhibits it. No longer observation interval or automatic fault clear was added. The motor model may advance to ALIGN/RUN in DRIVER_CHECK, but physical PWM remains off: **no phase switching or motor spin is expected on J4 in this build**.

Restore PCB_TEST_AXIS=0 to compare J3 using the same DRIVER_CHECK stage and history setting. J4 success would point toward an issue local to J3; matching failures would leave shared supply, configuration and measurement factors under investigation. No J4 hardware result or assembly diagnosis is established yet.
