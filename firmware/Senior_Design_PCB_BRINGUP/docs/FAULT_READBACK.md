# Motor-disconnected fault-register capture

**Historical diagnostic procedure:** active-image selections below are superseded by [the post-repair baseline](POST_REPAIR_OUTPUT_CHECK.md); temporary experiment flags are now off.

2026-10-23. J4's first-trip record identifies an EXTI nFAULT edge at 21 ms, with PE6 low, driver_ready=true and pwm_authorized=true. The earlier successful SPI status read occurred before that event. Its zeros do not identify the later fault. The user proposed resoldering again; obtain fault data first because no particular bad joint or chip has been established.

[TI SLVSE39B](https://www.ti.com/lit/ds/symlink/drv8304.pdf), sections 7.4/7.6, documents fault/status reporting and that ENABLE-low sleep disables SPI. The previous firmware immediately lowered ENABLE, losing the opportunity to read fresh fault status. The new optional bench path inhibits PWM immediately but briefly preserves the already-awake selected driver for two foreground read-only SPI frames before lowering ENABLE. Driver protections stay enabled. No CLR_FLT, reset pulse, profile write, retry or new PWM authorization occurs.

## Active bench settings

The existing active CubeIDE project is prepared as:

```c
#define PCB_STAGE PCB_MOTOR_RUN
#define PCB_TEST_AXIS 1
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 1
```

**Both motors must be disconnected for this diagnostic image.** Repository/source-archive defaults are axis 0 / IO_CHECK / both bench options off. Capture=1 is compile-time restricted to MOTOR_RUN. Restore capture=0 before connecting a motor or selecting another bring-up stage; this restores the normal immediate ENABLE-low response. The driver profile, gain 20, gate-drive current and protection bits are unchanged.

## Procedure and new readings

1. Build/flash the active Debug project and Resume. Use Live Expressions; let the capture finish before halting/stepping.
2. Release both buttons, center VR1, press/release S1 and move slightly outside neutral to reproduce the existing fault. PWM remains inhibited after the trip; the software fault stays reset-only.
3. Read **the new `g_driver_fault`**, especially:

```c
g_driver_fault.state
g_driver_fault.read_mask
g_driver_fault.reg[0]
g_driver_fault.reg[1]
g_driver_fault.elapsed_us
```

`DRIVER_FAULT_COMPLETE` and read_mask=3 mean both read transfers completed while the bench window remained permitted. These are raw fault bits, not a successful driver-health verification. The old `g_driver_status` still holds the normal startup/profile snapshot. `g_driver_trip` still identifies the original trip path and pre-inhibit pin/authorization state.

| State | Meaning |
| --- | --- |
| DISABLED | Capture option is off; normal immediate ENABLE-low behavior |
| WAITING | No eligible board-layer driver trip captured yet |
| PENDING | PWM off; selected driver briefly retained awake for reads |
| COMPLETE | Two permitted status transfers completed; inspect both registers |
| SPI_ERROR | Transport failed; use read_mask to identify any valid entry |
| TIMED_OUT | Window expired; ENABLE lowered and late replies discarded |
| STOPPED | S2/STOP canceled the window and lowered ENABLE |
| ABORTED | Another board-layer fault canceled the window |

read_mask is 0 for no completed status entry, 1 for register 0 only, and 3 for both. A zero in an unread entry is not a fault-free reading. SPI has no slave acknowledgement/CRC here: all-high responses can still indicate an unresponsive bus or lost supply even if HAL completed the clocks. If capture is canceled mid-transaction, late data is discarded rather than presented as valid post-fault status.

## Decode the two registers

| Register / mask | TI meaning |
| --- | --- |
| R0 `0x400` | Summary FAULT |
| R0 `0x200` | MOSFET VDS overcurrent summary |
| R0 `0x100` | Gate-drive fault summary |
| R0 `0x080` | VM undervoltage |
| R0 `0x040` | Overtemperature shutdown |
| R0 bits 5..0 | VDS overcurrent for HA, LA, HB, LB, HC, LC respectively |
| R1 bits 10..8 | Sense overcurrent A, B, C respectively |
| R1 `0x080` | Thermal warning |
| R1 `0x040` | Charge-pump undervoltage |
| R1 bits 5..0 | Gate-voltage fault for HA, LA, HB, LB, HC, LC respectively |

Use both registers together and the measured waveforms. For example, a gate-voltage flag can motivate checking a specific gate connection/waveform, whereas CPUV calls for VCP relative to VM. These are decoder definitions, not a diagnosis of the current board. The shared nFAULT line can be asserted by the other driver or disturbed electrically; a clean selected-driver capture does not independently attribute the line event.

## Shutdown and timing boundary

The IRQ records the original trip and clears both clocked timers' MOE/BIE before opening the capture window. In 6-input mode, the configured idle states keep all six selected PWM inputs low. Only a configured, already-awake driver whose ENABLE was requested high is eligible. During capture the firmware never reasserts that ENABLE; it merely preserves its existing state and forces the other ENABLE low. Fault latching invalidates normal SPI wake permission, preventing in-flight normal configuration from restoring readiness.

Two status reads run only in foreground, with interrupts enabled. The capture transport permits only `0x8000`/`0x8800` (read addresses 0/1), retaining normal CS spacing and the 2 ms per-frame HAL timeout. Success or SPI failure lowers both ENABLEs. S2 and other board-layer faults cancel immediately. A 5000 us budget, measured with TIM2 rather than SysTick, is checked before/after reads and by inhibition on the continuing 1 kHz control ticks. With no foreground service, ENABLE is lowered on the first such tick at/after the deadline. This is a running-firmware bound with control-tick granularity, not a measured hard 5.000 ms guarantee. A debug halt can suspend software enforcement; do not halt while PENDING. All MCU PWM is already off during that window.

Capture does not clear the software fault or resume motion after recovery; reset is required for another attempt. Default-off builds retain the original immediate PWM-and-ENABLE shutdown. No new hardware result or agent flash is claimed.

Tests cover both capture settings, raw status-bit preservation, interrupted reads, read-only foreground transport, immediate PWM inhibition, first-trip preservation, SPI failures, STOP before/during a frame, non-driver abort, timeout with/without foreground, late-data rejection and TIM2 wrap. They use simulated registers/transport, not real electrical timing. See `fault_readback_evidence.json` and [VALIDATION.md](VALIDATION.md).
