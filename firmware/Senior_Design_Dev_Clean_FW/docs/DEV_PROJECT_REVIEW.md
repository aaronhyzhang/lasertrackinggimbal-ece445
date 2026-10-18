# H753 dev project review

Latest update: 2026-10-08. The historical Oct 7 review below describes the original generated project, not the current implementation. DEV_FW now builds the analog/UDP voltage servo, VCP console, asynchronous sensors, PWM deadtime/break and watchdog. The inherited IOC is archived and hand-owned board.c defines current settings. See [current board record](../../Senior_Design_DEV_FW/docs/BOARD_CONFIG.md), [firmware guide](../../Senior_Design_DEV_FW/docs/FIRMWARE_GUIDE.md), and [software/bench validation](../../Senior_Design_DEV_FW/docs/VALIDATION.md). No physical board tests or flash performed.

Reviewed: 2026-10-07.
Project: [Senior_Design_DEV_FW](../../Senior_Design_DEV_FW).
IOC: [Senior_Design_DEV_FW.ioc](../../Senior_Design_DEV_FW/docs/archive/Senior_Design_DEV_FW.pre-demo.ioc).
IOC SHA-256: `afc7643b9a0b802b45ba8197355ddd2375a0792203b055acc4c5185e44b5bfca`.

Scope: source/configuration review of the new sibling CubeIDE project, including IOC, generated main/MSP/interrupt/HAL configuration, project metadata and linker scripts. No source/configuration changes, build, flash or hardware tests were performed.

The new project correctly targets STM32H753ZIT6, but its peripheral and pin configuration largely carries the custom PCB mapping. It is a generated starting point, not yet a Nucleo motor-control application.

## Correct target setup

- IOC part: STM32H753ZIT6.
- Debug/Release MCU define: STM32H753xx.
- Startup: startup_stm32h753zitx.s.
- Flash linker script: STM32H753ZITX_FLASH.ld.
- .project name: Senior_Design_DEV_FW.
- Separate sibling project preserves the H743 project's generated code.
- No shared application linked resources are present yet.
- main's foreground loop is empty; no motor control, encoder transactions, ADC start/calibration or PWM start is implemented.
- HAL Ethernet initialization exists, but there is no LwIP/application transport or PHY-service implementation.

## Findings in implementation order

### 1. Correct the Nucleo clock source before relying on any timing

[main.c](../../Senior_Design_DEV_FW/Core/Src/main.c), lines 193–203, uses HSE_ON, PLLM=2, PLLN=12, PLLP=2. stm32h7xx_hal_conf.h line 109 assumes HSE_VALUE=25 MHz.

The unmodified Nucleo normally uses an external ST-LINK MCO signal rather than a fitted 25 MHz MCU crystal. Verify the actual clock/bridges, choose HSE bypass and regenerate the whole PLL/ADC/peripheral clock tree for that measured source. Merely changing HSE_VALUE does not correct the PLL input-range and peripheral timing settings.

Current 75 MHz CPU/timer and 50 MHz selected peripheral figures are calculations under the project's 25 MHz assumption, not measured board clocks. All derived baud/PWM rates below have that same qualification.

ST reference: [UM2407](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf). Actual ST-LINK MCO configuration can vary; record it on arrival.

### 2. Establish the first-demo pin map rather than retaining every custom pin

| Function | Current generated choice | Dev-board action |
| --- | --- | --- |
| Analog command | ADC1 channel 15 / PA3 | Good initial candidate: A0, CN9 pin 1. Verify actual board/harness, then reserve it for 0–3 V input |
| First motor PWM | TIM1 six outputs PE9/PE8/PE11/PB0/PE13/PE12 | Candidate representative six-input harness; verify accessible header pins/bridges and EVM PWM mode |
| First encoder | I2C1 PB6/PB7 | Candidate; external sensor wiring and effective pull-ups need verification |
| First driver enable/fault | PG4 / PE6 | Proposed custom-like allocation only; verify EVM header wiring and reserve interrupt/shutdown path |
| Serial diagnostics | PD8/PD9 currently plain inputs | Reassign USART3 for ST-LINK virtual COM if used |
| Unneeded second-axis enable | PG7 output, initialized low | Shares Nucleo USB overcurrent wiring; remove unneeded custom assignment |
| Unneeded driver CS1 | PA9 output, initialized low | Shares USB VBUS sensing; H EVM needs no SPI CS |
| Custom LED | PD10 output, initialized low | Shares USB power-enable wiring; remove/remap as appropriate |

Check bridge-dependent USB connections (SB76, SB23, SB77) before driving shared pins. USART3 conflicts with the custom-board button roles; keep board configurations separate.

ADC2 currently converts PF14/channel 6. UM2407 warns against using PF14 as an ADC input in the Nucleo connector configuration. Do not base the demo's analog input on that allocation. ADC3 currently converts PF4/channel 9. A GPIO marked analog is not automatically part of an ADC conversion sequence.

No final Nucleo-to-BOOSTXL header-to-header harness has been produced or validated. Do that before connecting PWM/enable/power; the LaunchPad BoosterPack is not plug-compatible with the Nucleo.

### 3. Make motor output initialization and shutdown explicit before PWM starts

main.c lines 757–803 and 835–881 configure TIM1/TIM8 with:
- Prescaler 0, edge-aligned upcounting, ARR 65535.
- PWM1 outputs with initial compare 0.
- DeadTime 0; break and break2 disabled.
- No PWM/PWMN start calls.

At assumed 75 MHz timer clock this is approximately 1.144 kHz, not the proposed 20 kHz PWM. Configure a deliberate carrier, duty preload/update boundary, output polarity and inactive states, then scope all outputs before attaching a motor. Zero MCU dead time does not by itself prove shoot-through because the gate driver has its own timing logic; validate the complete selected PWM mode.

PE6 nFAULT is currently a plain input without an interrupt/shutdown implementation. Datasheets confirm TIM1_BKIN2 on PE6 for H753/H743; reserve/configure this proposed active-low hardware-break route after checking the EVM harness, and latch faults with automatic output restart disabled. Error_Handler disables interrupts and spins; exception handlers similarly have no explicit motor inhibition. Once a timer is driving, a spinning CPU can leave the last duty running. Add an ISR-compatible inhibition hook, fault latch and watchdog policy before actuation. Verify the EVM's actual fault behavior and reset/debug-halt behavior.

The existing low-level enable GPIO initialization is useful but is not an implemented supervisor or proof of reset-time electrical safety. See the custom ENABLE pull-up finding in [hardware reference](HARDWARE_REFERENCE.md).

### 4. Simplify acquisition for the analog milestone

- ADC1: software-triggered single rank, PA3/channel 15; sampling time 1.5 cycles.
- ADC2: software-triggered PF14/channel 6.
- ADC3: software-triggered PF4/channel 9.
- No ADC calibration/start or periodic acquisition is present.
- I2C1/2/3 are initialized but have no encoder read service; no corresponding DMA/IRQ acquisition path is configured.
- UART and IWDG HAL modules are disabled.

For the first slice, use one command ADC with adequate source-settling time, calibration, actual-reference scaling and sample timestamps. Show meter-matched readings at 0/0.75/1.5/2.25/3 V before motor power. Keep 3.0 V command full scale separate from ADC VREF. Start one encoder, record angle/status/age and verify hand rotation/wrap. Enable diagnostics before tuning.

Do not add all custom sensing/current-loop complexity to the first demo. When current FOC is implemented, redesign acquisition with timer triggers and valid low-side sampling windows.

### 5. Fix Ethernet configuration when the analog milestone is stable

stm32h7xx_hal_msp.c lines 309–343 use custom TX_EN=PB11 and TXD0=PB12. The Nucleo onboard PHY uses PG11 and PG13; TXD1 remains PB13. PG11 is also listed as ordinary GPIO input and should be removed from that assignment when adopting the correct RMII mapping.

RMII GPIOs are configured at LOW speed; review against the required signal edges. Initialization alone does not establish a network link or command channel.

RX/TX descriptors have named .RxDecripSection/.TxDecripSection attributes in main.c, but the linker script has no explicit placement for those sections. MPU_Config is the generated broad default region, not a descriptor/payload-specific DMA memory policy. No cache-enable calls were found. Define descriptor and payload placement, alignment and cache/MPU policy; verify final addresses in a build map rather than assuming orphan-section placement.

Then add PHY/link service and LwIP NO_SYS=1 using raw UDP callbacks, foreground packet input and periodic sys_check_timeouts, followed by the common command adapter and host client. Socket/netconn APIs require a different OS profile. Keep networking out of the motor timing path.

### 6. SPI and CAN are future/custom-board configuration, not demo prerequisites

SPI2 currently uses 4-bit data, mode 0 and prescaler 2 (25 MHz under the assumed kernel clock), in main.c lines 708–712. Both custom nCS GPIOs PE5/PA9 initialize low. This is incompatible with the custom DRV8304S's transaction/timing requirements and selects both devices if wired as the PCB. Correct the S backend to complete 16-bit transactions, suitable phase/speed and initially inactive selects before custom-board use.

BOOSTXL-DRV8304H uses hardware configuration; do not use the custom SPI settings as its setup mechanism. Prefer disabling unnecessary SPI/second-axis initialization in the first demo profile.

FDCAN1 has classic CAN configured with nominal prescaler 16 and segment1/segment2 = 1/1. At assumed 50 MHz this gives about 1.042 Mbit/s, not a conventional 1 Mbit/s configuration. FIFO/filter/TX allocations are zero, and no CAN application exists. Defer it; the Nucleo also needs an external transceiver.

## Recommended first implementation slice

1. Regenerate clocks and pin assignments for the actual Nucleo board/revision.
2. Bring up repeatable flash/boot and serial STATUS with a measured timebase.
3. Implement physical inhibit + fault handling before any output-start call.
4. Read PA3 analog input and one AS5600 with calibrated values, validity and age.
5. Establish a reviewed six-PWM/enable/fault/encoder harness and verify PWM on a scope.
6. Add low-energy calibration and encoder-oriented voltage control, then the bounded position servo.
7. Add Ethernet configuration/stack/client after analog move/hold is repeatable.

Maintain generated settings in the IOC, with small USER CODE hooks into application modules. Put shared application/control/device code in one place, then link it into the sibling dev project with explicit include paths. Build each target with only its own generated main, startup, linker and board configuration. No linked-source changes have been made during this review.

## Evidence and validation status

Reviewed .ioc/.project/.cproject, Core/Src/main.c, stm32h7xx_hal_msp.c, stm32h7xx_it.c, Core/Inc/stm32h7xx_hal_conf.h, main.h, startup selection and flash linker script. Compared IOC/peripheral intent with the H743 project and schematic. No compiled map, binary or bench measurements were available; no build was run. Findings about future actuation describe readiness gaps, not evidence that the currently empty application drives a motor.

References: [ST UM2407](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf), [TI DRV8304 datasheet](https://www.ti.com/lit/ds/symlink/drv8304.pdf), [TI BOOSTXL user guide](https://www.ti.com/lit/ug/slvub97/slvub97.pdf), [ST Ethernet memory example](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Applications/LwIP/LwIP_HTTP_Server_Socket_RTOS/readme.txt).

## Working-target and research decision — 2026-10-07

This is the active build/flash project. New shared application/control/device modules initially live here; the H743 build later links them instead of copying them. Canonical planning docs remain in ../Senior_Design_FW/docs and are linked from the dev README. See [ARCHITECTURE_RESEARCH.md](ARCHITECTURE_RESEARCH.md) for vendor-backed reasoning and changes to sensor timing, shutdown, current acquisition, networking and SDK integration. No IOC/source corrections have been applied by this documentation update.
