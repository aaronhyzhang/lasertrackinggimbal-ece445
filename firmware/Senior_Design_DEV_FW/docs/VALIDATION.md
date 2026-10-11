# Validation record

Updated 2026-10-08. Software evidence only; no flash or physical bench validation performed.

## Software checks

| Check | Evidence / status |
| --- | --- |
| ARM GCC Debug | Complete ELF/BIN/HEX/map build; application `-Wall -Wextra -Werror` |
| ARM GCC Release | Complete optimized `-O2` build with same warning policy |
| CubeIDE native Debug | Headless import/build using separate temporary IDE workspace; zero errors/warnings |
| CubeIDE native Release | Verified native Release (-Os): zero errors/warnings |
| Portable C execution | Native compiler tests the actual `motor_control.c`, `demo_protocol.c` and `demo_app.c` with a host-only board boundary |
| Client | Python unit tests for angle validation and loopback POSITION/HEARTBEAT/DISABLE/ACK exchange |
| DMA map | `.eth_dma` starts at 0x30000000; end/map size recorded below; all DMA objects explicitly aligned and placed |
| Stack policy | 8 KiB linker reserve; `.su` compiler output generated; runtime high-water measurement pending |

C tests include calibration with both encoder signs and stopped-rotor rejection; 0↔360° unwrapped trajectories, reference speed/acceleration and q-voltage bounds; reversal; following-error fault; encoder/health/ADC/foreground/command expiry; hard faults; ARM/CLEAR_FAULT/DISABLE behavior; angle/uint32 time wrap; strict protocol framing, integer overflow, NUL/trailing data rejection and sequence wrap/replay checks. Application tests also verify pending-action cancellation, immediate stop, source ownership, new-target-before-heartbeat and fault/lease restart behavior. The motor tests use synthetic encoder inputs, not an identified physical plant.

The Python loopback test does not run firmware LwIP, the STM32 MAC/PHY, EVM or encoder. Software compile/tests do not establish electrical correctness, protection current or physical tracking performance. This code follows vendor-backed architecture practices; it has not undergone formal MISRA compliance, functional-safety certification or independent product qualification.

## Required bench acceptance

| Gate | Result | Evidence to record |
| --- | --- | --- |
| Harness, EVM mode/protection straps, power rails, reset ENABLE pull-down | Pending | Received board revision, continuity, voltages, strap/BOM values |
| VCP boot, reset and watchdog | Pending | Banner/status, watchdog reset time, inhibited outputs |
| AS5600 health, raw wrap, filter configuration, effective latency | Pending | Hand rotation logs, I2C captures, ages, DIR and supply |
| ADC 0/0.75/1.5/2.25/3 V | Pending | Meter comparison, actual VDDA and raw endpoint scaling |
| Six PWM/deadtime/inactive states | Pending | Six input captures and EVM gate behavior, measured carrier/deadtime |
| nFAULT hardware/software shutdown | Pending | Injection, MOE/gate/ENABLE captures, latency, no automatic restart |
| Calibration sign/offset/return check | Pending | Direction, movement magnitude, supply current and resulting state |
| Analog gentle move/hold/full turn/reversal | Pending | Rehearsed targets, error/settling, current/temp and correct endpoint path |
| DISABLE / UART `!` / sensor loss / supply interruption | Pending | Physical coast/inhibit timing and restart sequence |
| Ethernet link, UDP owner/replay/invalid packets | Pending | VCP/status/client logs; source IP/port/session behavior |
| Client death/link loss/foreground load | Pending | ≤500 ms command lease + one tick; timing maximum, watchdog/fault behavior |
| Demo rehearsal | Pending | Operator sequence, configured limits and evidence captures |

Use the canonical [bench-session template](../../Senior_Design_FW/docs/DEV_BOARD_VALIDATION_PLAN.md#bench-session-record-copy-for-each-run). Complete analog acceptance before Ethernet motion. Limit changes require repeating affected checks.

## Final build evidence

This section is populated from the final artifacts, rather than the intermediate builds. ELF/BIN/HEX artifacts are under `build/<configuration>/`; native CubeIDE artifacts are under `Debug/` and `Release/`. None has been flashed.

Final standalone memory usage: Debug flash 99,268 B; Release (-O2) flash 59,248 B. Release D1 usage 56,616 B (includes reserved heap/stack); D2 DMA 19,264 B, range 0x30000000–0x30004B3F, end symbol 0x30004B40. Native CubeIDE Release uses -Os rather than the standalone script's -O2 and also passes; native Debug uses -O0. Both final native configurations report zero errors/warnings.

Final C control/protocol/application tests and both Python tests pass. [build_evidence.json](build_evidence.json) records standalone binary hashes and application/test/tool source hashes. Detailed native build logs remain in the original local workspace and are not committed here. These hashes identify the software snapshot; they do not certify hardware behavior.

## Repository checkout verification — 2026-10-11

The source-only repository checkout passed the C control/protocol/application tests and both Python client tests. Debug (-O0) and Release (-O2) rebuilt successfully with application warnings treated as errors. All 27 recorded source hashes and both rebuilt BIN hashes match the previously validated snapshot. Generated images, caches and local logs are excluded from Git. Physical bench validation remains pending.
