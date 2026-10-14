# Standalone open-loop demo validation

Updated 2026-10-14. Software verification only; the extracted image has not been flashed or bench-validated by the agent.

## Host checks

Tools/test.py executes the actual open-loop controller and actual physical RUN application with a host-only board boundary. Both pass with warnings treated as errors. Coverage includes signed map symmetry/endpoints and invalid inputs, speed bounds/ramp and electrical angle wrapping in both directions, center deceleration, coast/re-alignment reversal, startup wait/dwell interruption, ADC/foreground freshness, RUN/stop cancellation, boot-high inhibition, latched faults/reset and unsigned time wrap.

An independent comparison against the original Senior_Design_DEV_FW physical profile passed across **84,462 application ticks**. State/fault class, ENABLE/PWM, waiting/LED state and direction match; generated speed, angle, vector voltage, target and all phase duties have **zero numeric difference** in the tested trace. The trace includes full-speed forward/reverse/neutral, failed short center qualification, alignment cancellation, stopped RUN requests, invalid/stale/overrange ADC, foreground interruption, driver/hard faults, software stop/reset and tick wrap. It uses synthetic inputs and a fake board, not a motor model or hardware.

## Build checks

Standalone Debug and Release pass with application -Wall -Wextra -Werror and no warnings. Header/source dependencies are entirely within this project and the ARM toolchain. The independent active copy also passes native CubeIDE Debug and Release clean builds with zero errors and warnings. Standalone flash usage is 38,972 bytes Debug / 24,048 bytes Release; reported D1 allocation is 9,536 bytes for both (including linker stack/heap reservations). No DMA region is allocated. Native builds use their IDE settings, including -Os for Release, so sizes differ. Source and binary identities are recorded in build_evidence.json.

A dependency-file audit found no include/source dependencies on Senior_Design_DEV_FW or middleware. An application/source and linked-symbol audit confirms removal of feedback/controller, network and I2C/USART initialization functions; standard weak device vector names are retained by vendor startup.

## Required bench check of the extracted image

- Confirm disabled reset/boot-high RUN, then fresh RUN plus continuous 200 ms center arming.
- Verify correct six input pins, 20 kHz complementary PWM/deadtime and PG4/nFAULT inhibition; measure gates safely with the motor disconnected before load checks.
- With the unloaded secured motor, try about +/-10 RPM commands first; verify rotation, current, temperature, center coast and RUN LOW stop.
- Verify the one-second disabled reversal pause and fresh alignment at low speed. It is not measured standstill.
- Recheck actual driver fault shutdown and fault/reset restart behavior.

The user reported motor rotation with the preceding development image. That report is not physical validation of this extracted build. Current regulation, measured speed and stall detection are absent.
