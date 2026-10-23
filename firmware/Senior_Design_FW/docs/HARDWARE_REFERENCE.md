# Custom PCB hardware reference and schematic review

Reviewed: 2026-10-07. Target: STM32H743ZIT6, two motor axes.
Source: [schematic PDF](../Datasheets_UserManuals/Schematic%20PDF_%5BNo%20Variations%5D%20(1)%20(1).pdf).
SHA-256: `2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144`.

All nine PDF pages were rendered and visually inspected, with higher-resolution views of the MCU, channel buffers, inverters and power. Page references below mean physical PDF page numbers, not the inconsistent sheet counters. This is a schematic-level review; no BOM, PCB layout, ERC report or assembled-board measurements were available. Population choices below include the user-confirmed initial assembly; physical inspection should still verify the built board. No firmware or hardware was changed.

## Confirmed initial assembly

User clarification on 2026-10-07: backup regulator U6 unpopulated; regulator EN-to-ground resistor R7 unpopulated; sensor-buffer bypass jumpers unpopulated. Thus U4 is the intended active 5 V regulator, its internal EN pull-up permits startup, and the sensor paths use their buffers. This assembly clarification supersedes ambiguous PDF population markings.

## Findings to resolve before custom-board motor power

| Priority | Evidence | Consequence / required check |
| --- | --- | --- |
| Resolved assembly intent; verify rails | p8: U4 EN resistor R7 and backup regulator U6 both confirmed unpopulated | U4 is the sole intended onboard 5 V source. Verify population and 5V0/3V3_D on first power. J2 pin 2 still connects directly to 5V0; review backfeeding before using an external 5 V source. |
| Before encoder bring-up | p9: J5 has CAN and four I2C signals, but no encoder supply or GND pins | Plan a separate encoder power/ground harness or connector revision. I2C signal wires alone are insufficient. Use the encoder module's documented supply interface. |
| Before motor power | p6–7: R24_I_0/1 = 5.6k pulls each driver ENABLE high | Reset/high-impedance MCU pins do not hold the driver in sleep. This does not itself prove the bridge switches, but software boot inhibition is not a hardware default-off guarantee. Verify inactive PWM/reset/debug/watchdog behavior and consider an enable pull-down/default-off revision. |
| Before motor power | p3/p5: shared nFAULT reaches PE6, confirmed by datasheet as TIM1_BKIN2-capable | Configure and measure TIM1 hardware break. Its handler also inhibits TIM8/both enables; TIM8 needs a separately verified hardware shutdown design. No automatic restart. Read both drivers later to attribute shared faults. |
| Charge-pump drawing corrected; startup pending | p6-7: C44_I_0/1 is 1 uF / 25 V between VCP and VM, not VM-to-GND | Nominal capacitor differential is about 10 V. Previous full-bus rating warning and short diagnosis were tracing errors; see the corrected charge-pump review below. |
| Resolved assembly intent; calibrate | p5: sensor-buffer bypass jumpers confirmed unpopulated | Buffered acquisition is the intended configuration. Verify population, offsets and settling on hardware. |
| Before current-controlled FOC | p6–7: low-side shunts; p5: filtered/buffered current outputs | Sampling must occur in valid low-side conduction windows. Gain 20 and expected 4 A do not establish a 4 A hardware trip. Verify amplifier configuration, ADC timing, offsets and protection independently. |

## Parts and board variants

| Function | Custom PCB, from schematic | Dev setup |
| --- | --- | --- |
| MCU | U1 STM32H743ZIT6, p3 | NUCLEO-H753ZI, STM32H753ZIT6 |
| Gate drivers | U3_I_0/1 DRV8304SRHAR, p6–7: SPI-configured S variant | BOOSTXL-DRV8304H: hardware-configured H variant |
| Ethernet PHY | U14 LAN8742A-CZ, RMII, p2 | Onboard Nucleo PHY; use Nucleo wiring |
| CAN | U2 TCAN1044AVDRQ1, p4 | External CAN transceiver needed if CAN is added |
| Current shunts | Three per inverter, WSLP1206R0140FEA, 14 milliohm, p6–7 | EVM values/gain require a separate scale record |
| Signal buffers | TLV9004 sections U7–U12, p5 | EVM circuitry is separate |
| EEPROM | U13 24LC16BT-I/OT, p3 | Not required for first demo |

Use separate board bindings and separate gate-driver configuration backends. The S variant needs register configuration/readback; the H variant needs a recorded strap configuration.

## Motor actuation and digital signals

MCU mapping from p3; phase labels follow the schematic. Validate pin alternate functions and phase order when configuring CubeMX.

| Signal | Axis 0 | Axis 1 |
| --- | --- | --- |
| Phase A high input | PE9 / TIM1_CH1 | PC6 / TIM8_CH1 |
| Phase A low input | PE8 / TIM1_CH1N | PA5 / TIM8_CH1N |
| Phase B high input | PE11 / TIM1_CH2 | PC7 / TIM8_CH2 |
| Phase B low input | PB0 / TIM1_CH2N | PB14 / TIM8_CH2N |
| Phase C high input | PE13 / TIM1_CH3 | PC8 / TIM8_CH3 |
| Phase C low input | PE12 / TIM1_CH3N | PB15 / TIM8_CH3N |
| Driver ENABLE | PG4 | PG7 |
| Driver CAL | PG5 | PG6 |
| Driver nCS | PE5 | PA9 |
| Firmware-driven enable LED | PE3 | PC10 |
| Firmware-driven fault LED | PE2 | PC11 |

Shared driver signals: SPI2 SCLK PB10, MISO PC2_C, MOSI PC3_C; nFAULT PE6. p5 shows 5.6k pull-ups on shared nFAULT (R23) and SDO/MISO (R10). The indicator LEDs are driven by the MCU; they are not direct proof of gate-enable state or fault status.

Set both chip selects high before any SPI clocks. Select one driver at a time. The driver can require ENABLE high to wake/configure SPI; keep PWM inputs in a verified non-driving state during this configuration interval. Distinguish permission to wake/configure the IC from permission to actuate a motor. Re-verify configuration after sleep/reset rather than assuming register retention.

For the S backend, start conservatively around 1 MHz, use complete 16-bit words and the specified falling-edge input sampling (mode 1 with idle-low clock). Verify readback and nCS timing on a scope. TI's timing table gives a 200 ns minimum SCLK period (5 MHz maximum), 600 ns minimum inactive nCS time, and 1 ms SPI-ready delay after enable. These requirements do not apply as an SPI setup procedure to the H EVM.

The MOSFET model printed by Q4 is SiS890ADN-T1-GE3; confirm all six fitted parts from the BOM. Shunt Kelvin routing and power-return layout cannot be assessed from this PDF. The inverter drawings show zero-ohm gate resistors; the 330 pF / 22 ohm snubbers are explicitly DNP. Tune the driver's gate-current/slew configuration using measured gate/phase ringing. Internal driver timing protection does not replace verification of MCU output polarity, PWM mode, disable behavior and switching waveforms.

## Encoder buses and EEPROM

| Device | Peripheral | SCL | SDA | Schematic pull-ups |
| --- | --- | --- | --- | --- |
| Axis 0 AS5600 connection | I2C1 | PB6 | PB7 | 2.2k to 3V3_D |
| Axis 1 AS5600 connection | I2C2 | PF1 | PF0 | 2.2k to 3V3_D |
| EEPROM U13 | I2C3 | PA8 | PC9 | 2.2k to 3V3_D |

The EEPROM's nets are labeled I2C2_SCL/SDA, but its PA8/PC9 pins correspond to I2C3. Bind by physical peripheral/pins rather than the net label. Separate motor buses accommodate the AS5600 fixed address 0x36.

EEPROM WP is tied high through populated-looking R66 = 0 ohm; R67 to GND is marked DNP. Treat it as write protected until assembly says otherwise. Do not plan automatic calibration writes to it in the default configuration.

## ADC map

Mappings below are custom-board bindings, not a Nucleo harness prescription. Channels were cross-checked against the generated custom IOC pin allocation where present; final acquisition groups/triggers are not implemented.

| Measurement | Axis 0 pin / candidate channel | Axis 1 pin / candidate channel |
| --- | --- | --- |
| Current A | PB1 / ADC1_INP5 | PC0 / ADC1_INP10 |
| Current B | PF11 / ADC1_INP2 | PA3 / ADC1_INP15 |
| Current C | PF12 / ADC1_INP6 | PA6 / ADC1_INP3 |
| Phase voltage A | PF5 / ADC3_INP4 | PF8 / ADC3_INP7 |
| Phase voltage B | PF6 / ADC3_INP8 | PF9 / ADC3_INP2 |
| Phase voltage C | PF7 / ADC3_INP3 | PF10 / ADC3_INP6 |
| Temperature | PF4 / ADC3_INP9 | PF3 / ADC3_INP5 |
| Local potentiometer | PA0 / ADC1_INP16 | PA4 / ADC1_INP18 |

Motor-bus sense: PF13 / ADC2_INP2, from 24V0_M through R79/R80 and U10 on p5. It measures the motor rail upstream of the logic-supply PI filter.

ADC1 entries identify usable channel candidates, not simultaneous sampling. A two-axis current loop needs deliberate allocation among ADC instances and synchronized conversion groups; a sequence through one ADC has time skew.

### Current scaling

p6–7 confirm 14 milliohm low-side return shunts and driver VREF = 3V3_A. Proposed gain 20 gives sensitivity 0.28 V/A. With verified bidirectional midpoint 1.65 V:

- Conversion: phase_current_A = (measured_volts - calibrated_zero_volts) / 0.28.
- At ±4 A: nominal output 0.53–2.77 V.
- Driver's specified output interval 0.25 V to VREF–0.25 V gives a nominal ±5 A interval with these values, before tolerances.
- Ideal ADC rail arithmetic gives ±5.89 A; that is not the amplifier's usable linear range.
- At 4 A, instantaneous shunt dissipation is 0.224 W while conducting; evaluate RMS heating and layout separately.

The SPI backend must verify gain and VREF-divider settings. Zero-current calibration is per phase and per axis. Low-side current is not valid in every PWM state.

The driver's lowest sense-overcurrent threshold is 0.25 V across a shunt: approximately 17.9 A with 14 milliohm. Therefore that feature alone does not implement the expected 4 A limit. Verify VDS-based protection separately and add calibrated operating-current supervision/current regulation as appropriate. The meaning of the user's 4 A figure (phase peak, RMS or supply) remains open.

### Filters and voltage scaling

| Circuit | Drawn values | Derived nominal behavior | Firmware implication |
| --- | --- | --- | --- |
| Current-output RC, p5 | 120 ohm series, 1 nF to GND before buffer | tau 120 ns, simple RC corner about 1.33 MHz | Total settling includes driver, buffer and ADC, not just this RC |
| Phase divider, p6–7 | 100k top, 10k bottom, 10 nF across bottom | V_ADC = V_phase/11; 24 V -> 2.182 V; Rth 9.09k, tau 90.9 us, corner about 1.75 kHz | Strongly filtered diagnostics; not an instantaneous PWM-resolved winding-voltage sample |
| Bus divider, p5 | R79 100k, R80 10k, C76 10 nF | Same nominal /11 scale and RC | Calibrate actual bus; verify buffer/bypass assembly |
| Pots, p3 | 10k series and 0.1 uF at ADC | At least approximately 1 ms RC, plus potentiometer source impedance | Longer acquisition time may be needed; schematic pots span 3.3 V, separate from demo's 3.0 V command full scale |
| Thermistors, p6–7 | 10k pull-up, 0.1 uF; RT1 part/value not legible | Voltage ratio available; temperature curve unknown | Need thermistor BOM resistance/Beta or table before reporting degrees C |

Examples of buffer bypasses: current R15/R20/R22 (axis 0), R14/R16/R21 (axis 1); temperature R52/R55; phase R54/R59/R63 (axis 0), R53/R57/R61 (axis 1); bus R83. All sensor-buffer bypass jumpers are confirmed unpopulated for the initial assembly; verify that on receipt. Unused op-amp sections also deserve a defined input termination review.

## Ethernet

p2: LAN8742A-CZ uses its own 25 MHz crystal Y2. nINT/REFCLKO goes through R45 = 33 ohm to MCU PA1. The intended PHY-to-MCU RMII reference is 50 MHz; verify this on the actual board along with strap readback.

| RMII / management signal | Custom MCU pin |
| --- | --- |
| REF_CLK | PA1 |
| MDIO | PA2 |
| MDC | PC1 |
| CRS_DV | PA7 |
| RXD0 / RXD1 | PC4 / PC5 |
| TX_EN / TXD0 / TXD1 | PB11 / PB12 / PB13 |

These TX pins differ from the Nucleo's PG11 / PG13 / PB13. Do not transfer the custom pin configuration unchanged.

PHYAD0 has a pull-down; address 0 is the initial probe candidate, verified by reading a valid PHY ID. Mode and clock-output straps must be checked against fitted resistors and link status. nRST shares MCU reset, so the drawing provides no independent MCU GPIO PHY reset. Use PHY register reset when appropriate.

J1 is an RJ45 with integrated magnetics. Layout, differential impedance, return paths and magnetics implementation were not reviewed. MCU HSE Y1 on p3 is separate from PHY Y2; Y1 frequency is not readable in this PDF. The custom IOC's 25 MHz assumption needs BOM confirmation.

## CAN

p4: TCAN1044AVDRQ1, 5 V VCC, 3.3 V VIO; MCU PA11 RX / PA12 TX. STB is tied to GND, so software has no standby pin. R2 permanently places 120 ohm across CAN_P/CAN_N as drawn. Confirm this board is an endpoint or choose an appropriate termination assembly. CAN ESD protection is drawn; bus topology/bitrate remain separate system choices.

## Power and connectors

Power path on p8: VIN -> reverse-protection D3 -> 24V0_M motor rail. L2 = 47 uH feeds 24V0 for logic regulators; parallel R4 = 0 ohm is marked DNP. TVS D4 is on the motor rail. The drawing does not establish a continuous regeneration/braking-energy solution; measure bus rise during deceleration and respect actual component ratings.

U6 and R7 are confirmed unpopulated initially: U4 is enabled by its internal EN pull-up and is the intended 5 V source.

5V0 -> LM1084 3.3 V LDO -> 3V3_D. Analog supply/reference derives through filtering on p3. LDO heat is approximately (5–3.3)*load_current, requiring a board thermal check.

| Connector / test point | Connection |
| --- | --- |
| J2 power | pin 1 VIN; pin 2 5V0; pins 3–6 GND |
| J3 motor axis 0 | pins 1/2/3 phase A/B/C |
| J4 motor axis 1 | pins 1/2/3 phase A/B/C |
| J5 IO | 1 CAN_N; 2 CAN_P; 3 encoder-1 SCL; 4 encoder-1 SDA; 5 encoder-0 SDA; 6 encoder-0 SCL |
| J6 SWD | 1 3V3; 2 SWDIO; 4 SWCLK; 10 NRST; 3/5/9 GND |
| TP1 / TP2 / TP4 / TP5 | motor rail / filtered logic input / 5 V / 3.3 V |
| TP6–TP9 | GND |

MCU buttons PD8/PD9 have pull-ups and switch to GND. LEDs PD12/PD11/PD10 are custom-board outputs; these assignments conflict with useful Nucleo functions and need separate bindings.

## Firmware consequences and open evidence

1. Two per-axis contexts, TIM1/TIM8 actuator ownership, separate encoder buses; common nFAULT can stop both.
2. Separate DRV8304H and DRV8304S configuration backends; driver wake/configure is distinct from authorization to drive.
3. Startup, reset and watchdog inhibition must be verified physically, given ENABLE pull-ups.
4. Current FOC needs synchronized ADC samples and phase/channel validation, not just enabling all analog pins.
5. Filtered phase voltage and bus voltage support diagnostics; measured phase-voltage feedback is not needed for the proposed encoder-oriented voltage controller.
6. No EEPROM writes until WP/assembly is resolved.
7. Encoder power/ground wiring, actual HSE crystal and thermistor BOM remain open. Regulator and buffer/bypass population intent is now confirmed above.
8. PCB layout, MOSFET/shunt thermal performance, component derating and fault propagation latency require later hardware review.

Related notes: [dev project review](DEV_PROJECT_REVIEW.md), [firmware architecture](FIRMWARE_ARCHITECTURE.md), [motor control design](MOTOR_CONTROL_DESIGN.md), [demo plan](DEV_BOARD_VALIDATION_PLAN.md).

## Assembled-PCB bring-up update - 2026-10-23

User confirms this PDF matches the received PCB, including DRV8304S and SiS890ADN-T1-GE3 MOSFETs. First open-loop target is J3 / TIM1, with VR1 and S1 ARM / S2 STOP, CSA gain 20 V/V and minimum gate-drive current. STLINK-V3MINI, 24 V bus, proposed 2 A supply limit. Power reportedly checks out; switching and numeric measurements remain pending.

**Pot numbering clarification:** VR1 is **POT1 / PA4 / ADC1_INP18 / TP12** through R84; VR2 is **POT0 / PA0 / ADC1_INP16 / TP13** through R85. The earlier ADC table lists net/pin candidates, not a required pot-to-motor-axis assignment. The new firmware reads VR1 first and uses it for J3. This mapping was rechecked against embedded schematic net metadata and the MCU package pins (40 and 34).

J3 MCU PWM test points are TP17/18 (A high/low), TP19/20 (B high/low), TP21/22 (C high/low); CAL0 is TP29 and ENABLE0 TP30. See [PCB_BRINGUP.md](PCB_BRINGUP.md) for the pin audit, hardware boundaries and link to the independent H743 bring-up firmware. The original generated SPI mode/speed and initially-low chip selects are not suitable driver configuration; the new target uses mode 1 at 781.25 kHz and verifies every register setting. J4 remains inhibited pending its own shutdown-path validation.

Vendor checks: [TI DRV8304 datasheet](https://www.ti.com/lit/ds/symlink/drv8304.pdf), [TI LM22676 datasheet](https://www.ti.com/lit/ds/symlink/lm22676.pdf), [Microchip LAN8742A datasheet](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf).

## Architecture research update — 2026-10-07

[H743 datasheet](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf) confirms PE6 TIM1_BKIN2 and shared ADC1/2 access for PB1/PA3. [ARCHITECTURE_RESEARCH.md](ARCHITECTURE_RESEARCH.md) records a candidate ADC1/2 pair per axis, shared acquisition scheduling, hardware-break implications and source applicability. These peripheral capabilities are verified; assembled-board timing/protection performance is not.

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
