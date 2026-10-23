# Software validation - 2026-10-23

This is software evidence for the H743 custom-PCB port. No agent flash, oscilloscope measurement, gate switching or motor spin on the custom PCB was performed. The prior H753 setup's user-reported motor rotation is the starting algorithm evidence, not validation of this board port.

## Checks completed

- The actual `open_loop.c` / `modulation.c` pass host tests for symmetric command mapping, neutral ARM, both field directions, bounded speed/voltage ramps, angle wrapping, neutral stop, coast/realign reversal, health faults, reset-only fault recovery and tick wrap.
- The actual `demo_app.c` passes with a fake board boundary in all four stage configurations: I/O cannot arm; boot-held S1 cannot arm; fresh S1 outside center waits for a full 200 ms valid dwell; stale/invalid samples restart that dwell; S2 and ISR stop cancel wake/align/run; a press overlapping STOP cannot become a delayed ARM; VR2 cannot command motion; reversal, foreground freshness and fault latching are exercised.
- The actual DRV8304S backend passes register/frame emulation: correct addresses/masks/profile, COAST during setup, every individual transaction failing, stuck-high/low MISO, fault status and register mismatch. These tests do not simulate physical SPI timing or the fitted driver.
- ARM GCC 13.3 builds pass with no warnings for Debug/Release in all four explicit stages plus the configured-default IO stage (10 images). CLI application sources use `-Wall -Wextra -Werror`; Debug is `-O0`, Release `-O2`.
- The independently copied active project imports and clean-builds in STM32CubeIDE 1.18.0, Debug and Release, **0 errors / 0 warnings**. Native Debug uses `-O0`, native Release the inherited `-Os`; ELF is the normal CubeIDE flash input.
- H743 device define, H743 startup vector and 2 MB H743 linker target are used. The new project has no source links to a Nucleo project. The Nucleo projects are unmodified by this port.
- The default remains `PCB_STAGE PCB_IO_CHECK`. Schematic reference-designator/net tracing resolved **VR1 = R84 / TP12 / POT1 / PA4 / ADC1_INP18**, not PA0. B-low returns to schematic PB0. J4 pins are outputs low and its driver remains disabled.

Source hashes, compiler output sizes and software-test scope are recorded in [build_evidence.json](build_evidence.json). Host test results do not verify the BSP's real timer/ADC/SPI registers, electrical pin continuity, propagation delay, phase current, thermal behavior or reset defaults. Those require the staged checks in [BRINGUP.md](BRINGUP.md).

The two-ADC pair is kept owned until both conversion callbacks commit, preventing a higher-priority control interrupt from starting a new pair while the HAL marks the previous conversion ready but its callback is still completing. The driver wake permission has a generation counter: STOP/fault invalidates in-flight foreground configuration before it can mark the driver ready. These paths were reviewed in the BSP; physical interrupt timing still requires bench validation.

## Fault-capture correction - 2026-10-23

Hardware bring-up exposed an early return at the summary fault before detailed status register 1 was read. The verifier now captures all registers before comparison and provides a read-valid mask. New regressions cover summary FAULT plus CPUV in register 1, summary-only fault inhibition, and failure of the second status read. The electrical fault remains unclassified pending a new complete hardware capture. Follow-up build/test evidence is in fault_capture_evidence.json.

## CPUV identified; charge-pump wiring review - 2026-10-23

User reports >100 MOhm between VCP and VM and axis-0 C44=1 uF / C50=22 nF populated. Reinspection corrects the earlier tracing error: C44 is VCP-to-VM, not a bus bypass, so there is no drawn direct short or missing storage capacitor. The previous full-bus rating warning and proposed rework are withdrawn. CPUV=0x040 remains the captured fault; electrical cause and startup waveforms are pending. See BRINGUP.md for the corrected component map and ENABLE/VCP single-acquisition procedure. No firmware source or protection changed; documentation checks replace software rebuilds for this correction.

## Temporary CPUV-history bench option - 2026-10-23

The driver verifier has a small compile-time exception for CPUV-only history. Tests exercise strict and exception policies, each other status bit combined with CPUV, configuration/transport failures, and live-driver-good loss during wake. The electrical driver profile and board.c GPIO/PWM/nFAULT paths are unchanged. The active bench copy uses MOTOR_RUN / exception=1 with motors disconnected; default repository settings remain IO_CHECK / exception=0. No new diagnostic state machine or automatic fault clearing was introduced. Follow-up evidence is in cpuv_bench_evidence.json; physical charge-pump and switching performance remains unverified.
