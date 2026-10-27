# System state machine

`Core/Inc/system_fsm.h`, `Core/Src/system_fsm.c`, `Tests/test_system_fsm.c`.

Gimbal-level states from the design document (initialization, calibration, idle,
searching, tracking, fault), layered **above** the two per-axis servos in
`position_control.c`. It is HAL-independent and **observes** the servos: it never
arms, calibrates or moves a motor. Arming stays physical S1; every servo protection
is unchanged. Its only action is a stop request.

Status: wired into the **dual-axis** build (`demo_dual.inc`) with the new `G1 TARGET`
command and `sys=` STATUS fields. Single-axis builds compile the module but do not use it
and refuse `TARGET`. Host tests and all four firmware build profiles pass; **not yet run
on the PCB.**

## States

```text
            init_done                  any axis WAKE_CAL/ALIGN/SWEEP/RETURN
   INIT ─────────────► IDLE ◄──────────────────────────► CALIBRATING
                        │  ▲
          S1: WAKE_RUN  │  │ all axes READY/OFF (S2, DISABLE, lease expiry)
                        ▼  │
                      ARMING ──► RUNNING ──┬─► TRACKING   fresh report, ball detected
                                           └─► SEARCHING  no / stale report, or not detected
                                                  │ optional search timeout → stop request
   any state: axis FAULT, board fault, e-stop, init failure ──► FAULT (latched until reset)
```

| State | Condition | Outputs |
|---|---|---|
| INIT | Until `init_done` | `stop` |
| CALIBRATING | Any axis in WAKE_CAL, ALIGN, SWEEP or RETURN | none |
| IDLE | No axis calibrating, waking or running | `can_arm` when both axes READY, calibrated and zeroed |
| ARMING | Any axis WAKE_RUN (takes priority over RUNNING) | none |
| TRACKING | Axis RUNNING and vision report ≤ `target_stale_ms` old with ball detected | none |
| SEARCHING | Axis RUNNING otherwise; servos hold their last target | `stop` after `search_timeout_ms` if non-zero |
| FAULT | Latched; first cause kept in `reason`, `fault_axis`, `axis_fault` | `stop` |

Defaults (`system_default_config()`): stale after 300 ms, search timeout 0 (hold
indefinitely). Holding is the default because DISABLE
releases torque and a loaded tilt axis can drop.

## Firmware integration (dual-axis build)

| Where | What |
|---|---|
| `demo_dual.inc` `app_control_tick()` | After both servos tick, builds `system_input_t` from both servos and the last TARGET report, calls `system_fsm_step()`. `stop` joins the existing stop path (`board_kill()`, `position_stop()`, targets invalidated) |
| `demo_dual.inc` `app_position()`, `app_calibrate()` | Refused while the system is FAULT, so nothing restarts before reset |
| `demo_dual.inc` `app_target()` | Stores `{seen, detected, ms}` under `board_lock()` |
| `demo_dual.inc` `app_status_text()` | Appends `sys=<state> sys_reason=<reason>` |
| `demo_protocol.c/.h` | New `NET_TARGET`: `G1 <session> <seq> TARGET <0|1>`; anything else is `ERR syntax` |
| `demo_network.c` | TARGET is owner- and sequence-checked like HEARTBEAT; it does **not** refresh the motor lease and cannot acquire ownership |
| `demo_app.c` (single-axis) | `app_target()` returns false (`ERR G1 ...`) |
| Live Expressions | `g_system` (state, previous, reason, fault_axis, axis_fault, entered_ms, transitions) |

Inputs not yet connected: `board_fault` (board trips already fault the servos) and
`estop` (no pin assigned on Rev1; passed as `false`). `init_done` is set because `app_init()`
runs after board and network setup. Behaviour change for existing users: none unless
a system FAULT latches (it already coincides with a servo FAULT, which needs a reset) or a
non-zero `search_timeout_ms` is configured (default 0).

### STATUS values

`sys`: 0 INIT, 1 CALIBRATING, 2 IDLE, 3 ARMING, 4 TRACKING, 5 SEARCHING, 6 FAULT.
`sys_reason`: 0 NONE, 1 INIT_FAILED, 2 AXIS_FAULT, 3 BOARD_FAULT, 4 ESTOP, 5 SEARCH_TIMEOUT.

### Laptop side

`host/gimbal_bridge/bridge.py` sends `TARGET 1/0` on the tilt port every cycle once
both axes run, but only if the board's STATUS contains `sys=`; against older firmware it
skips TARGET and works as before. Without TARGET reports the board shows SEARCHING
while running.

## Tests

`Tests/test_target_protocol.c`: TARGET parsing (valid 0/1, nine malformed forms, existing
commands unchanged). `Tests/test_system_fsm.c`, assert-style, 7 groups: boot/calibration/`can_arm`,
arming/tracking/searching/stale vision, stop back to IDLE, search timeout and restart,
e-stop latch, fault latching and first-cause priority, 32-bit millisecond wrap.

MSVC (Build Tools 2022, from this directory, in a Developer Command Prompt):

    cl /nologo /std:c11 /W4 /WX /I Core\Inc Tests\test_system_fsm.c Core\Src\system_fsm.c /Fe:test_system_fsm.exe && test_system_fsm.exe
    cl /nologo /std:c11 /W4 /WX /I Core\Inc Tests\test_target_protocol.c Core\Src\demo_protocol.c /Fe:test_target_protocol.exe && test_target_protocol.exe

Zig, matching `Tools/test.py` (not run yet; zig was not installed on the verifying machine):

    zig cc -std=c11 -O1 -UNDEBUG -Wall -Wextra -Werror -I Core/Inc Tests/test_system_fsm.c Core/Src/system_fsm.c -o test_system_fsm.exe

Verified 2026-10-27: both C tests pass under MSVC `/W4 /WX`; an injected stale-limit bug is
caught; `Tools/build.py` builds dual Debug axis 1 / Ethernet, dual Release axis 0 / Ethernet,
single Debug default and single Release axis 0 / Ethernet with `-Werror` and no warnings
(none flashed); host bridge tests (19) pass including TARGET and older-firmware cases;
Colin's Python client tests pass except `test_client_commission.ModelTests`, which needs
`scipy` (not installed here, unrelated). Not run: `Tests/test_network.c` and the rest of
`Tools/test.py` (need zig), and anything on the PCB.
