# J4 high-side source-current comparison

2026-10-24. The user authorized a 15 -> 45 mA high-side turn-on-current experiment after synchronized startup still produced J4 R0=0x608/R1=0. This is a diagnostic comparison, not a confirmed fix.

## Single configuration change

`PCB_HS_SOURCE_MA` selects 15 (previous baseline) or 45. TI Table 16 defines IDRIVEP_HS=000 as 15 mA and 010 as 45 mA. Only register 3 changes: 0x300 -> 0x320. LOCK stays 011 and high-side sink remains 30 mA. This affects all three high-side channels in the selected driver; the DRV8304 has no per-phase source-current setting. Low-side source/sink stay 15/30 mA. Registers 2/4/5/6, TDRIVE, dead time, OCP thresholds/mode, CSA gain, wake timing, synchronized start, neutral guard, STOP and fault latching remain unchanged. No SPI electrical/timing setting changed.

Expected verified profile at 45 mA:

| Address | Hex | Decimal |
| --- | --- | --- |
| 0/1, before a fault | 0 / 0 | 0 / 0 |
| 2 | 0x080 | 128 |
| 3 | 0x320 | 800 |
| 4 | 0x300 | 768 |
| 5 | 0x300 | 768 |
| 6 | 0x280 | 640 |

Normal configuration writes are individually read back; periodic verification uses the same selected profile. A baseline 0x300 response when 0x320 is requested is rejected, not accepted as healthy. Raw fault captures still read only status addresses 0/1 and do not clear any protection.

## Active bench setup and steps

Existing Senior_Design_PCB_BRINGUP project, Core/Inc/pcb_config.h:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 1
#define PCB_PWM_SYNC_START 1
#define PCB_HS_SOURCE_MA 45
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 1
```

Keep both motors disconnected and retain the same current-limited 24 V bench supply. Clean/build/flash the existing Debug image and Resume. Center VR1, release both buttons, press/release S1, then move VR1 slightly outside neutral. Confirm stage=3, axis=1, verified=true and g_driver_status.reg[3]=800. If reg[3] remains 768, check the flashed image/selected config before interpreting the comparison.

If the bridge remains healthy, observe J4 phase outputs to board GND and report the result; do not attach a motor to this diagnostic capture build. If it faults, record g_driver_fault.state/read_mask/reg[0]/reg[1], plus the first input/nFAULT edges if available. A changed result would suggest gate-drive/switching sensitivity, not prove the physical root cause. An unchanged 0x608 means this setting did not resolve the B-high VDS event; do not bypass the protection.

To return to the previous source-current profile, change only PCB_HS_SOURCE_MA to 15 and rebuild. Active Core before this experiment is saved under Senior_Design_FW/tmp/pcb_bringup/before_hs45/Core. Repository defaults remain IO_CHECK, axis=0, capture=0, source=15. No agent flash or hardware result is claimed.

## SPI bus and configuration audit

The reviewed schematic physical page 5 joins both inverter sheets to the same SCLK_M, SMOSI_M and SMISO_M nets. They are two chip selects on one bus, not separate SPI peripherals. The bus grouping drawn on the sheet does not create another controller.

| Function | Shared MCU connection / per-driver difference |
| --- | --- |
| SCLK_M | PB10, SPI2_SCK AF5 |
| SMOSI_M | PC3_C, SPI2_MOSI AF5, analog switch closed |
| SMISO_M | PC2_C, SPI2_MISO AF5, analog switch closed |
| J3 nCS | PE5 |
| J4 nCS | PA9 |

There is one hspi2 handle, one SPI initializer and one transfer routine. Both axes use mode 1 (CPOL low, CPHA second edge), MSB-first 16-bit frames, nominal 781.25 kHz clock, software CS, at least 1 us CS setup/hold and at least 2 us between selects. The inactive CS is raised before selecting the active driver. Driver register setup, COAST during configuration, readback checks and wake prerequisites are identical; the chosen CS/ENABLE route differs. The unselected driver is held disabled.

SPI can still fail electrically on one branch (chip-select joint, SDO loading, local supply/ground or switching interference). SPI here has no slave acknowledgement/CRC; a completed HAL transfer alone is not proof of a valid reply. However, exact control-register verification and repeated coherent detailed 0x608 captures lower the likelihood of a basic mode/clock/word-size mismatch. They do not rule out transient communication errors or an incorrect but consistently selected slave. Checking SCLK/MOSI and both chip selects at startup, or capturing the 0x9800 register-3 read and 0x320 data, would be direct bus evidence. No SPI clock or phase change is bundled into the gate-current test, preserving one changed variable.

## Software verification

The host suite now runs 22 executables: the previous 18 plus four additional 45 mA driver/backend variants covering both axes and CPUV-history settings. Both source profiles are checked for exact encoding, protection register preservation, rejected mismatched source readback, transport failures and unchanged fault handling. Ten compile guards include unsupported source-current values. Native active J4 Debug/Release builds pass without warnings. These checks are software evidence, not gate-voltage or SPI signal-integrity measurements.

Sources: TI SLVSE39B sections 7.5/7.6 and Table 16 (https://www.ti.com/lit/ds/symlink/drv8304.pdf); existing nine-page PCB schematic physical pages 3/5/6/7, SHA-256 2d538235485d3f2e2b304468b9ff6afa6fb7fc8a22f0789b6c701709f734a144; current board.c, pcb_pins.h and drv8304.c.
