# Protocol tests

These tests run your actual `Core/Src/protocol.c` on the computer. They require
Python 3 and a host C compiler, not CubeIDE, LwIP or a connected board.

From the repository root:

```sh
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_protocol.py
```

To also detect memory access errors and undefined behavior:

```sh
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_protocol.py --sanitize
```

Use `--cc /path/to/compiler` to choose a different host compiler. Build and run
failures return a nonzero exit status. Temporary executables are removed afterward.

Coverage:

- Command values, signs, whitespace and 32-bit numeric boundaries.
- Missing/extra fields, invalid booleans/numbers and unsupported versions.
- Unchanged output on parsing failure.
- Explicit byte lengths, non-null-terminated input, embedded NUL and 128-byte limit.
- Exact telemetry text, flags, newline, terminating NUL and reported send length.
- Insufficient buffer capacity, boundary writes and invalid pointers.

The parser accepts the full numeric range, including session/sequence zero and
nonzero errors when detection is false. Session policy, stale sequences, target-loss
behavior and timeouts are tested separately below. Physical limits belong to the
application and control layers.

The six groups do not test Ethernet connectivity or motor behavior. The many
individual checks include checking each byte beyond a telemetry buffer's capacity;
they are not separate communication scenarios.

Keep `Tests/` outside `Core/`: the current CubeIDE configurations compile only
`Core/` and `Drivers/`, so this test's `main()` is not linked into MCU firmware.

## Command-handler tests

From the repository root:

```sh
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_command_handler.py --sanitize
```

The runner compiles the actual `command_handler.c` and `protocol.c`. The ten groups cover:

- Initialization and empty observation state.
- First/newer commands, skipped sequences and copied command fields.
- Duplicate/old/wrong-session rejection without changing stored data or receive time.
- Sequence rollover, reverse ordering and the ambiguous half-range boundary.
- Freshness immediately before and at the timeout, plus same-session refresh.
- Millisecond-counter rollover.
- Target loss, stale-state precedence and reacquisition.
- Session ownership retained after timeout and takeover only after explicit reset.
- Invalid pointers and invalid timeout settings.
- Parsing-to-handler integration, including not forwarding malformed packets.

Unusable observations leave the caller's output untouched. Callers must use it only
when the function returns `OBSERVATION_VALID`. Tests compare fields rather than
structure padding. The simulated clock makes timeout tests deterministic.

These tests do not prove that the application inhibits motors, restricts resets to
disabled operation, or detects observation age before packet receipt. Those behaviors
are outside this handler. Restoring a valid observation after a timeout is not motor
arming; any required rearming interlock belongs in the application.

## Network and telemetry tests

```sh
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_network.py --sanitize
python3 firmware/Senior_Design_Dev_Clean_FW/Tests/test_udp_client.py
```

Telemetry tests check empty/fresh/stale/target-lost/reset status and invalid arguments;
positions always remain invalid and armed stays false. The network suite compiles
the real UDP endpoint and LwIP, injects real IPv4/UDP messages into an in-memory
interface and checks the outgoing replies and destination. It covers HELLO, STATUS,
VISION, bad input, session/IP/port ownership, sequence rejection, timeout and explicit
bench-only session reset. The Python tests check the client's wire format and status
decoder. These do not exercise the board's PHY, Ethernet DMA or physical cable.
