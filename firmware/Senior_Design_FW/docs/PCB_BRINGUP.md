# Custom PCB initial bring-up

Started 2026-10-23. Target MCU: STM32H743ZIT6. This record separates schematic facts, software verification and measurements still needed on the assembled board.

## Source and applicability

Reviewed source: `Datasheets_UserManuals/Schematic PDF_[No Variations] (1) (1).pdf`, nine physical pages, SHA-256 `2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144`. Pages 3 (MCU/auxiliary), 5 (interconnect), 6/7 (inverters) establish the bindings below. The title blocks do not identify a usable board revision. On 2026-10-23 the user confirmed the assembled PCB matches this file, including DRV8304S, with no changes reported. [Earlier hardware review](HARDWARE_REFERENCE.md) records the complete circuit review and user-confirmed initial population intent.

The user reports the power rails have been checked and look good. Actual measured rail voltages, current draw and power-stage switching measurements have not been recorded here. Do not treat a software build as evidence of assembled-board operation.

## Verified schematic bindings

| Function | Axis 0 / J3 | Axis 1 / J4 | Peripheral / polarity |
| --- | --- | --- | --- |
| Phase A high | PE9 | PC6 | TIM1_CH1 AF1 / TIM8_CH1 AF3 |
| Phase A low | PE8 | PA5 | TIM1_CH1N AF1 / TIM8_CH1N AF3 |
| Phase B high | PE11 | PC7 | TIM1_CH2 AF1 / TIM8_CH2 AF3 |
| Phase B low | **PB0** | PB14 | TIM1_CH2N AF1 / TIM8_CH2N AF3 |
| Phase C high | PE13 | PC8 | TIM1_CH3 AF1 / TIM8_CH3 AF3 |
| Phase C low | PE12 | PB15 | TIM1_CH3N AF1 / TIM8_CH3N AF3 |
| ENABLE | PG4 | PG7 | Active high; external 5.6 kOhm pull-up |
| CAL | PG5 | PG6 | External pull-down; keep low for this test |
| Driver nCS | PE5 | PA9 | Active low; deselect both before any clocks |
| Driver-enable indicator | PE3 | PC10 | MCU-driven, active high |
| Driver-fault indicator | PE2 | PC11 | MCU-driven, active high |
| Pot net labels, independent of selected motor | PA0 / POT0 / **VR2** / TP13 | PA4 / POT1 / **VR1** / TP12 | ADC1_INP16 / ADC1_INP18 |

Shared: nFAULT = PE6 (TIM1_BKIN2 AF1, active low); SPI2 SCLK = PB10, MISO = PC2_C, MOSI = PC3_C (AF5). The PC2/PC3 analog switches must connect the `_C` pads to their digital GPIO paths; the new binding sets that explicitly. **VR1 is POT1/PA4, despite being the first reference designator.** Trace: VR1 pin 2 -> R84 -> TP12/POT1 -> U1 pin 40 / PA4. VR2 -> R85 -> TP13/POT0 -> U1 pin 34 / PA0. This was checked in both the drawing and embedded net metadata.

| Auxiliary function | Schematic pin | Behavior established by circuit |
| --- | --- | --- |
| S1 / AUX_BUT_0 | PD8 | 10 kOhm pull-up, pressed = low |
| S2 / AUX_BUT_1 | PD9 | 10 kOhm pull-up, pressed = low |
| Blue LED1 / LED_EN_0 | PD12 | High turns MOSFET/LED on |
| Green LED2 / LED_EN_1 | PD11 | High turns MOSFET/LED on |
| Red LED3 / LED_EN_2 | PD10 | High turns MOSFET/LED on |
| Reset S3 | NRST | Hardware reset, not a software button |

Both onboard pots span the schematic's 3V3_D rail, with 10 kOhm series resistance and 100 nF ADC filtering. The Nucleo demo's 3.0 V command full scale and 3.15 V invalid threshold cannot be copied without adapting the physical pot range. Raw ADC checks should precede motor authorization.

## Changes required from the development firmware

- Use STM32H743xx device headers, H743 startup vectors and H743 linker target. Keep the Nucleo H753 projects independently usable.
- Axis 0 B-low returns to **PB0**, as routed on the PCB. PE10 was a Nucleo-specific change to avoid its LED load.
- Replace PG2 jumper RUN with confirmed S1/PD8 ARM and S2/PD9 STOP. PG2 is not an onboard RUN control in this schematic.
- Read the onboard pot(s), not Nucleo PA3. PA3 is an axis-1 current-sense signal on the PCB.
- Replace the DRV8304H strap procedure with DRV8304S SPI configuration and readback. Use 6-input PWM; configure both CS pins high at startup. Configure only a deliberately selected stage.
- Keep the other inverter's ENABLE and all six PWM inputs low throughout one-axis testing.
- Use an internal HSI clock for initial bring-up so the unreadable HSE crystal frequency is not guessed.
- Retain neutral qualification, alignment, field-speed ramp, reversal pause, fault latching, watchdog and fresh ADC requirements from the proven open-loop core.

The original generated H743 project is a peripheral scaffold, not the existing motor application. Its SPI configuration uses first-edge sampling and approximately 25 Mbit/s; TI specifies falling-edge sampling and at most 5 MHz. Its startup also initially asserts both driver chip selects low. Those settings must not be reused for driver bring-up.

## Hardware issues that software cannot resolve

1. **Reset default:** ENABLE pull-ups mean the drivers can be awake before firmware takes ownership. Scope all PWM inputs and ENABLE during power-on, reset, connection under reset, debug halt and watchdog reset. Prefer a documented hardware default-off rework if it is needed; software cannot guarantee pin levels while held in reset.
2. **Charge-pump startup:** C44_I_0/1 is the 1 uF / 25 V VCP-to-VM storage capacitor. C50 is the 22 nF flying capacitor. The earlier full-bus rating warning was a tracing error; current CPUV needs an ENABLE/VCP startup capture, not presumed capacitor rework.
3. **Fault path:** PE6 provides direct hardware break to TIM1. The drawing does not route it to a TIM8 break input. Do not describe axis 1 as having the same independent hardware shutdown as axis 0 without a verified connection/rework or another validated design.
4. **Current limit:** 14 mOhm shunts and CSA gain 20 are measurement scaling, not a 4 A trip. DRV8304 sense-OCP at its lowest threshold is about 17.9 A. Open-loop voltage operation does not regulate phase current. Record bench current limits and measure winding current during alignment/run.
5. **Assembly:** earlier intent was U6 backup regulator, R7 regulator-EN pull-down and all sensor-buffer bypass jumpers unpopulated. Confirm any changes on the assembled revision.

## Confirmed bring-up choices

- PCB matches the PDF, DRV8304S and SiS890ADN-T1-GE3 MOSFETs confirmed; no assembly changes reported.
- J3 / axis 0 first; **VR1 / PA4** is the motor command. J4 remains disabled; VR2 is diagnostic only.
- Center-stop bidirectional control retained. Normalize full onboard ADC travel to the earlier 0..3 command, so nominal physical neutral is **1.43..1.87 V**, centered at 1.65 V. Endpoints command -100/+100 RPM.
- S1 ARM request, S2 immediate latched STOP; release/repress required after boot; ARM waits for a valid neutral pot for 200 ms; faults require reset.
- CSA gain **20 V/V**, minimum gate current **15 mA source / 30 mA sink**. Software explicitly writes and reads back the driver profile.
- STLINK-V3MINI on J6; 24 V bus, proposed 2 A bench supply limit. This is a DC supply limit, not a measured/regulated phase-current limit.
- C44 is drawn across VCP-to-VM and sees nominally about 10 V; its drawn 25 V rating exceeds TI's 16 V recommendation. The earlier VM-to-GND identification and full-bus margin warning are withdrawn.

## Bench sequence

1. Motor disconnected: check SWD target voltage, BOOT0/reset and MCU identity. First flash should exercise only LEDs, buttons and raw potentiometer ADC readings, holding both ENABLEs and all PWM inputs low.
2. Confirm button polarity, LED identities, both pot minima/maxima/center readings and both ENABLE-low voltages. Record results before assigning motor commands.
3. With ENABLE held low, verify the selected stage's six MCU PWM signals at the inverter test points: 20 kHz carrier, complementary pairs with dead time, correct pin/phase identities and low outputs when stopped. The unused stage remains low.
4. Motor disconnected: wake/configure the selected DRV8304S with PWM off, read back the chosen registers, check nFAULT and gate-to-source waveforms. Check stop/fault/reset behavior before connecting a motor. Use a differential probe for high-side VGS, or two ground-referenced channels and math with both ground clips at board ground.
5. Power off before connecting an unloaded motor to the selected connector. Use the already identified FIT1036 and a deliberately current-limited supply. Start at neutral, request ARM, then small speed commands; validate alignment, ramp, stop and fault handling. Check phase current, rail transients and device temperature before increasing effort/speed.
6. Validate the second stage separately after its shutdown path is established. Closed-loop sensing, Ethernet and CAN are later milestones.

## Primary references

- [TI DRV8304 datasheet](https://www.ti.com/lit/ds/symlink/drv8304.pdf), SLVSE39B, sections 6.6, 7.4, 7.5, 7.6.
- [ST STM32H743 datasheet](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf), DS12110 Rev 11, pin/alternate-function tables.

The separate [Senior_Design_PCB_BRINGUP project](../../Senior_Design_PCB_BRINGUP/README.md) implements these choices. Its default first-flash selection is IO_CHECK; one line selects PWM_CHECK, DRIVER_CHECK or MOTOR_RUN after the corresponding checks. [Detailed procedure](../../Senior_Design_PCB_BRINGUP/docs/BRINGUP.md) includes the corrected test points, programmer wiring, expected diagnostics, driver profile and a bench-results table. No agent flash or custom-board motor validation has been performed.

## First flash result - 2026-10-23

User reports flashing now succeeds after correcting the flipped connector orientation. The earlier "no device found on target" connection issue is resolved. This confirms user-reported programmer/target connection and flashing; LED/button/ADC behavior, gate-driver configuration, PWM and motor operation remain to be checked. The key opening itself is not a pin-1 marker; STDC14 contacts 3..12 map to J6 contacts 1..10. No firmware change was needed to resolve the connection issue.

User subsequently reports the IO_CHECK LEDs/buttons and VR1 ADC behavior look good. **Only VR1 is currently soldered; VR2 is unpopulated.** VR2/PA0 is diagnostic-only and may float; its sample value does not command the motor or qualify neutral. Both conversions must still complete for the pair's freshness flag. Numeric VR1 endpoints/center, measured ENABLE-low levels and switching remain unrecorded. Leave the selected stage under deliberate bench control; next intended check is PWM_CHECK with motors disconnected and both ENABLEs low.

PWM_CHECK follow-up: user initially saw ENABLE high because CubeIDE was paused before initialization; after Resume both ENABLEs measured low. The approximately 1.8 V VR1 setting was inside neutral, and a scope channel initially was not selected. User now reports all three MCU phase PWM outputs look good. Numerical carrier/dead-time measurements have not been supplied. Next check: DRIVER_CHECK SPI/readback with PWM held off; then MOTOR_RUN with motors still disconnected to inspect gate/phase switching before connecting a motor. J3 is the inverter power output, not the MCU PWM test connector.

DRIVER_CHECK follow-up: user reports red fault LED, `g_driver_status.verified=false` and `g_app_status.driver_good=false`. Active header was checked as DRIVER_CHECK. The two booleans do not identify the originating fault: inhibition clears readiness after a fault. Pending evidence is `g_app_status.motor.fault`, expanded `g_driver_status` (failed address and stored registers), and measured driver VM/motor-bus voltage. Read registers in Live Expressions while running; do not reset before collecting the latched snapshot. Entries not reached by a failed configuration may still be zero and do not prove absence of driver faults. No cause or fix is established yet.

## Complete driver-fault capture - 2026-10-23

User supplied VM=23.7 V (after reverse-polarity diode) and a live snapshot: FAULT_DRIVER_CONFIG, failed_address=0, registers [1024,0,128,768,768,768,640]. Registers 2..6 match the intended profile (0x080,0x300,0x300,0x300,0x280). Register 0=0x400 is summary FAULT; its VM-UVLO bit is clear. The original verifier returned at address 0, so register 1's zero was unread initialization data. Increasing supply voltage is not indicated by this capture.

The diagnostic correction reads all verification registers before comparing fault/readback values and adds g_driver_status.read_mask (0x7f for a complete verification snapshot). Faults still inhibit and latch; transport errors still fail capture. Added regressions ensure detailed register-1 faults are captured even when register 0 contains summary FAULT, summary-only faults are rejected, and an unread status register is marked invalid. No cause or hardware fix is established. Active stage remains the user's DRIVER_CHECK selection. Reflash and obtain both status registers with the updated snapshot.

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

See the [startup capture procedure](../../Senior_Design_PCB_BRINGUP/docs/BRINGUP.md#next-measurement-charge-pump-startup).

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
