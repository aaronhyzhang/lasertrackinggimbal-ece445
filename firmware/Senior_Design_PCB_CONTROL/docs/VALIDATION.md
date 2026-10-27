# Validation record, 2026-10-26

Latest requirement audit after a 26-us first-callback SERVO_TIMING capture:
voltage mode uses a 45-us execution budget within the 50-us trigger period;
current PI retains 25 us. The 40..65-us sample-interval and duplicate-sequence
rejections apply only to current PI. Both modes retain 150-us current freshness,
calibrated finite/range-checked currents and per-phase 1.5-A trips. Current-sum
inconsistency stays diagnostic in voltage mode; it trips current PI as before.
Voltage calibration counts settled commanded-vector dwell, then validates
encoder sweep distance, direction and return. Current-vector sign/magnitude
qualification remains current-PI-only. Encoder transport/MD/freshness/jump,
STOP, driver nFAULT, board deadlines, supply/thermal limits, command freshness,
following error, voltage/duty caps and bounded travel remain enforced.
Regression tests accept a 26-us voltage callback and reject a 46-us callback;
controller tests distinguish voltage-mode jitter/sum handling from PI mode and
verify stale and overcurrent shutdown still work. Hardware retest pending.
Older notes below describe earlier commissioning behavior.

Latest voltage-mode behavior: at the user's request, the callback-time timer
window is diagnostic-only when CONTROL_CURRENT_PI_ENABLED=0. Timer-triggered
ADC acquisition, neutral/duty limits and ADC settings are unchanged. Calibration,
freshness, clipping, analog validity, phase-current/current-sum trips, driver
protection and control execution budgets remain enforced. Future current PI
still requires the callback window. Eight acquisition cases cover both axes,
revisions V/Y and both regulation settings (12 BSP executables total). Earlier
notes below describe the investigation before this change. Board retest pending.

Current-window retest captured descending CNT=385, below the 1650..2450
callback acceptance window, with corrected ADC prescaler /2. Native and CLI
Debug builds previously used -O0. CONTROL Debug now uses -O2 with debug symbols
to reduce interrupt and control-path latency; Release settings are unchanged.
Live Expressions globals remain available; optimized local stepping may differ.
This is a latency correction candidate, pending board confirmation. The snapshot
is callback-time, not a hardware timestamp of the ADC aperture. Window limits,
25-us callback budget and all fault protections remain unchanged.

Software checks pass; assembled-board control/Ethernet commissioning is pending.
No image was flashed by the agent. The working BRINGUP project was not changed
by this development increment.

Boot timing fix: a scripted TIM7 interrupt between `board_us()` and the read of
`last_tick_us` reproduced an unsigned-age underflow and false
`FAULT_CONTROL_TIMING`. The watchdog now snapshots both values under one lock.
The regression passes after the fix and still trips for a genuinely stale
control tick. Board timing/foreground faults are no longer mislabeled as
SERVO_DRIVER, and the first servo fault is preserved during shutdown. This fix
does not require an Ethernet cable and does not disable timing protection.

Calibration feedback investigation: explicit tests accept encoder status 0x33
(detected + weak) with the existing commissioning allowance. First-fault captures
now retain the failing input, prior state and encoder-rejection reason, including
ADC freshness/failure masks and errors, even when subsequent readings recover.
Missing magnet and invalid analog data still fault. The 20 kHz PWM-control path
is skipped while inhibited/waking; acquisition and 1 kHz supervision continue.
The captured SERVO_FEEDBACK input identifies ADC1 regular-scan failure with an
accepted encoder. ADC1 stalled at rank 2 of its first frame, with ADSTART set,
EOC interrupt enabled, no overrun and no HAL error. The clock configuration had
not accounted for revision V's additional fixed ADC /2: at 6.25 MHz a regular
rank takes 63.36 us and is starved by the 50-us injected trigger. Prescaler is
now /2 on revision V, /4 on older revisions, giving the intended 12.5 MHz analog
clock, 31.68-us regular rank and 6-us injected frame. Host tests check both
revision paths and both axes. Hardware confirmation is still pending; analog
validation and fault shutdown remain enabled.
Reference: ST RM0433, ADC clocks and triggered injection sections:
https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf

Board retest confirms regular ADC acquisition recovered (fresh mask 7, failed
mask 0, ADC1 count increasing). Calibration now trips SERVO_FEEDBACK in ALIGN:
driver/encoder/analog/zero are good, but the injected current window is invalid.
The first-fault input now preserves current_timer_count, current_timer_cr1 and
current_adc_clock_prescaler from the rejected sample. Tests verify timer values
remain tied to that sample when the live counter changes. No window limits or
protections were relaxed. The reported 41-us max fast callback also exceeds the
25-us drive budget and requires follow-up; it did not replace the first feedback
fault. Successful calibration and motion remain unvalidated.

- Portable controller/protocol tests: motion-disabled voltage mode, active voltage
  mode, and the separately selected future current PI algorithm. Checks include
  both encoder directions, separate mechanical zero, non-wrapping 350->10 travel,
  trajectory acceleration/endpoint behavior, voltage amplitude independent of
  measured current in voltage mode, current-loop feedback sign in the optional
  PI mode, vector/duty saturation, stop, freshness/continuity, command lease,
  supply/temperature/current/sum/timing trips, parser bounds and replay sequence wrap.
- Eight scripted-HAL integration executables: three actual application/button
  configurations, ADC acquisition for both selected axes on revisions V and Y, and the actual Ethernet
  endpoint linked to real LwIP IPv4/UDP. Covers stopped-release calibration,
  immediate STOP, no running re-zero, ADC channel/trigger configuration, rank-paced
  regular scans, all conversions/calibration, injected-current sign and window
  rejection, freshness/error handling, custom RMII pin mapping, PHY identification,
  link transitions, UDP ownership/replay/checksum/stop, and RX pbuf recycling.
- Five Python client tests with real localhost UDP sockets: measured-position
  acceptance, ACK without settling fails, timeout sends DISABLE, status-only query,
  wrong-axis rejection and malformed status handling.
- Six standalone ARM build profiles: Debug/Release inhibited axis0; Debug active
  axis0/pot; Release active axis0/Ethernet, axis1/pot and axis1/Ethernet. Current PI
  is disabled in every production build in this matrix.
- Native CubeIDE clean Debug and Release builds in an isolated IDE workspace:
  zero errors and warnings. Ethernet DMA descriptors/buffers link into aligned
  D2 SRAM and not DTCM. The source copy installed beside the original project is
  checked against the repository development copy.

`validation_evidence.json` records source, ELF and log hashes. Standalone test and
build logs are under `build/validation` in the development repository copy.

These are software tests with scripted hardware boundaries. They do not validate
the physical PHY, clock, ADC sample settling, current scale/polarity, motor gains,
calibration motion, braking/thermal behavior or bounded stopping. Follow
COMMISSIONING.md in order before treating position operation as demonstrated.
