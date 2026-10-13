# Nucleo / BOOSTXL wiring and configuration

Updated 2026-10-12. This is the implemented pin allocation; continuity and waveform checks on the actual board revision remain pending. No BoosterPack stacking: use a labelled harness.

## Harness

Nucleo morpho locations below are from UM2407 table 22. TI locations are from SLVUB97 tables 3/4. Confirm pin 1 orientation and the received board's revision before connecting. Use the MCU signal name as the definitive allocation.

| Function | H753 signal | Nucleo header | BOOSTXL-DRV8304H |
| --- | --- | --- | --- |
| Phase A high input | PE9 / TIM1_CH1 | CN12 pin 52 | J4 pin 1 / INHA |
| Phase A low input | PE8 / TIM1_CH1N | CN12 pin 40 | J4 pin 3 / INLA |
| Phase B high input | PE11 / TIM1_CH2 | CN12 pin 56 | J4 pin 5 / INHB |
| Phase B low input | PB0 / TIM1_CH2N | CN11 pin 34 | J4 pin 7 / INLB |
| Phase C high input | PE13 / TIM1_CH3 | CN12 pin 55 | J4 pin 9 / INHC |
| Phase C low input | PE12 / TIM1_CH3N | CN12 pin 49 | J4 pin 11 / INLC |
| Driver ENABLE | PG4 | CN12 pin 69 | J5 pin 9 / ENABLE |
| Driver nFAULT | PE6 / TIM1_BKIN2 | CN11 pin 62 | J5 pin 15 / nFAULT |
| Common ground | GND | CN11 pin 19 or CN12 pin 39 | J4 pin 2 or J5 pin 4 / GND |
| EVM digital I/O supply | 3.3 V | CN11 pin 16 | J5 pin 1 / VDDIO |
| Analog command (position or speed by mode) | PA3 / ADC1_INP15 | CN12 pin 37; also A0, CN9 pin 1 | Separate 0–3 V source, common GND |
| Encoder SCL | PB6 / I2C1_SCL | CN12 pin 17 | FIT1036 encoder SCL |
| Encoder SDA | PB7 / I2C1_SDA | CN11 pin 21 | FIT1036 encoder SDA |
| Encoder supply | 3.3 V / GND | Verified Nucleo supply pins | FIT1036 labelled encoder supply/GND |

Connect the three motor winding leads to EVM phases A/B/C. Do not infer encoder GH1.25 connector order from wire colours: verify its labels/pinout separately. DFRobot specifies a 3.3 V encoder supply. The encoder is independent of the 24 V winding supply. Pull SCL/SDA up to 3.3 V (effective 2.2–4.7 kΩ starting range, considering existing board resistors); scope rise time on the real harness.

Supply the EVM motor power terminal from the 24 V bench supply, with a suitable fuse, conservative current limit and accessible power cutoff. Power the Nucleo from ST-LINK USB. Keep the motor-current return out of the thin signal-ground harness. J5 pin 1 supplies VDDIO; it is not permission to join unrelated regulator outputs. Verify EVM logic rail/strap population before powering.

Add an effective ~10 kΩ ENABLE-to-ground resistor at the EVM so reset/unpowered MCU means disabled. nFAULT is open drain: confirm an effective pull-up to **3.3 V**, adding about 4.7 kΩ if needed. Inspect existing pulls before adding or overriding them. A 10 kΩ PA3-to-ground pull-down is useful for a disconnected target lead, although unplug still looks like a valid 0° command.

## DRV8304H straps

Use **6-PWM mode**, with MODE at AGND as specified in the TI datasheet. CAL stays at ground for this voltage-only demonstration. J4 pin 13 is MODE; J4 pin 20 is CAL. Configure the actual EVM strap components rather than shorting a populated opposing driver/source. H hardware-config pins are not the S variant's SPI interface.

GAIN (J4 pin 18), IDRIVE (J5 pin 18) and VDS (J5 pin 20) retain a deliberately verified EVM hardware configuration; this firmware does not drive or program them. Record their resistor/jumper population before motion. High-impedance GAIN selects 20 according to TI, but the EVM shunt value and VDS threshold still need verification; the custom PCB's 14 mΩ value does not apply automatically. Gate-driver VDS protection is not a calibrated 4 A current limit.

## Implemented clock/peripheral settings

| Peripheral | Setting |
| --- | --- |
| Clock source | HSI 64 MHz; no ST-LINK MCO/HSE dependency |
| PLL1 / CPU | M=16, N=100, P=2: 200 MHz CPU; LDO, VOS1 |
| AHB / APB | 100 MHz AHB; all APBs 50 MHz; TIMPRE reset/default gives 100 MHz timer kernels |
| ADC kernel | PLL2 M=16 N=50 R=4: 50 MHz, ADC async /4 = 12.5 MHz |
| TIM1 | Center aligned, ARR=2500, 20 kHz nominal; CH1/2/3 plus complements; preload commits together |
| Deadtime | 50 timer ticks, nominal 500 ns; verify complete EVM switching behavior on scope |
| TIM1 break | PE6 BKIN2 active low, unfiltered, automatic output restart off, configuration locked at level 1 |
| TIM7 control | 1 kHz nominal; measured spacing checked against 700–1500 µs, execution budget 500 µs |
| TIM2 timebase | 1 MHz, 32-bit unsigned elapsed-time arithmetic |
| ADC1 | PA3 channel 15, 16 bits, 64.5 sample cycles, boot calibration, periodic software-triggered IT |
| I2C1 | PB6/7, 7-bit AS5600 address 0x36; TIMINGR=0x40932F35, approximately 100 kHz, verify on scope |
| AS5600 | Raw angle 0x0C/0D; separate STATUS 0x0B every 20 ms; volatile CONF=0x0300, fastest slow filter, read back at boot; no OTP writes |
| USART3 | PD8 TX / PD9 RX, 115200 8N1, ST-LINK VCP; default USART3 solder bridges must be fitted |
| Ethernet | Nucleo LAN8742, PHY address 0; PA1/2/7, PB13, PC1/4/5, PG11/13, AF11 |
| Watchdog | IWDG1, LSI /32, reload 999: nominal ~1 s, LSI tolerance applies; no debug freeze |
| Fault indicator | Nucleo LD3, PB14, high on software fault |

The AS5600 remains powered continuously. Angle reads are asynchronous and launched once per servo tick when I2C is idle; health reads replace occasional angle reads. Expect occasional 2 ms angle spacing. A failed boot configuration requires fixing the encoder wiring and rebooting. There is no automatic active-drive I2C recovery; stale data inhibits drive.

Priorities: hardware break 0; UART 1 (for immediate `!`); control 2; I2C/ADC 3; SysTick 4; Ethernet 5. Short interrupt-masked publications keep sample/action tuples coherent. No wait, allocation, printf, or network processing runs in the servo ISR.

During wake/sleep, TI specifies transient nFAULT assertion. Hardware break stays enabled and MOE remains clear while waking. Break IRQ notification is enabled only after nFAULT is high and drive is authorized, avoiding an IRQ storm during the expected wake transition. A later break clears MOE in hardware and latches software inhibition; it never automatically restarts.

## Memory and source ownership

I-cache is enabled; D-cache remains disabled throughout this demo. `.eth_dma` explicitly holds 32-byte-aligned descriptors and persistent RX/TX pools in D2 SRAM, starting at 0x30000000; the linker enforces a 256 KiB bound. Buffers are initialized explicitly because the section is NOLOAD. TX slots remain owned until HAL reports descriptor completion; LwIP pbuf memory is copied into those DMA slots. No DMA payload points into DTCM. Enabling D-cache later requires MPU/cache-line policies and renewed network tests. Flash linker reserves 8 KiB stack; hardware watermarking is still required.

## Source and generation ownership

This demo is a **hand-owned CubeIDE C project using STM32 HAL**. `Core/Src/board.c` is the authoritative peripheral initialization. `main.c`, interrupt handlers and MSP hooks are also hand-owned. The CubeMX project nature was removed; the original IOC is preserved under `docs/archive/` as historical evidence and does not describe the running firmware. Do not regenerate it into the demo project.

The old generated main/MSP/IRQ/config/linker/project files were backed up in the original local CubeIDE workspace; those local backups are not committed here. The historical IOC is retained under `docs/archive/`. Application/protocol files remain separate from HAL/CMSIS. A later migration back to CubeMX must create a new IOC matching this record, generate into a temporary project, compare every clock/pin/IRQ/timer/memory setting, and preserve application modules. The H743 project configuration has not been replaced with this Nucleo BSP.

References: [ST UM2407, connectors and VCP](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf), [TI SLVUB97, EVM headers](https://www.ti.com/lit/ug/slvub97/slvub97.pdf), [TI DRV8304, mode and wake behavior](https://www.ti.com/lit/ds/symlink/drv8304.pdf), [DFRobot FIT1036, motor/encoder parameters](https://wiki.dfrobot.com/fit1036/), [AS5600 datasheet](https://look.ams-osram.com/m/7059eac7531a86fd/original/AS5600-DS000365.pdf).

## Open-loop profile assumptions — 2026-10-12

No new pin/harness allocation is needed. PA3 becomes a one-way speed command only when mode OPEN_LOOP is selected; fixed positive A/B/C sequence, 11 pole pairs, 24 V configured bus, 6-PWM straps and 3.3 V digital interface remain assumptions to verify on the bench. Encoder may be absent for open-loop drive; its power/pulls and boot readback must work before closed-loop calibration. PB6/PB7 have no fitted Nucleo external I2C pull-ups; inspect the encoder-board pulls or provide appropriate pulls to 3.3 V. EVM current/protection settings remain unverified and are not replaced by a voltage clamp. Full limits, mode sequence and a correction table are in [OPEN_LOOP_GUIDE.md](OPEN_LOOP_GUIDE.md).
