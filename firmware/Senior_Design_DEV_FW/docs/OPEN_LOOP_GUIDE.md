# Open-loop firmware and mode selection

For the full working-principles explanation, see the [LaTeX guide and separate diagrams](open_loop_control/README.md).

**INLB uses PE10 / CN12 pin 47**, replacing the old PB0 / CN11 pin 34 connection. See [phase-B diagnostics](PHASE_B_PWM_DIAGNOSTIC.md).

Current v0.5.0 default: **bidirectional open-loop motor control using PG2 RUN and the PA3 pot**, without serial commands, encoder or Ethernet. RUN requests wait for 1.3–1.7 V continuously for 200 ms before arming. See [OPEN_LOOP_BOARD_CONTROL.md](OPEN_LOOP_BOARD_CONTROL.md).

Updated 2026-10-13. Use the [board-control guide](OPEN_LOOP_BOARD_CONTROL.md) for the current wiring, mapping, startup, reversal and bench procedure. Motor rotation and the preceding 100 RPM range are user-reported; this bidirectional revision still needs hardware checks.

## One-line mode selection

In Core/Inc/demo_config.h, DEMO_DEFAULT_OPEN_LOOP=1 selects analog signed speed and 0 selects closed-loop 0–360 degree position. Rebuild normally in CubeIDE or run python Tools/build.py --config Debug; the header controls normal builds. DEMO_OPEN_BOARD_CONTROL=1 selects physical PG2 RUN for open loop. Both motor modes boot disabled; closed loop needs encoder calibration and ZERO. This does not change the DRV8304H hardware MODE strap, which remains 6-PWM.

## Control pieces

| Piece | OPEN_LOOP | CLOSED_LOOP |
| --- | --- | --- |
| Analog input | Signed speed: 0 V=-100 RPM, 1.3–1.7 V=0, 3 V=+100 RPM | 0–3 V -> 0–360 degree position |
| Electrical angle | Accumulated from commanded field speed | Encoder times pole pairs plus calibrated offset |
| Regulators | Speed/voltage ramps only | Position P, velocity PI and bounded voltage effort |
| Encoder | Not used | Required, with magnet/freshness checks |
| Startup | Center-neutral arming, timed stationary-field alignment | Calibration sign/offset/return validation, ZERO, ARM |
| Mechanical travel | Continuous unloaded bench rotation | One-turn reference and travel/tracking checks |
| Actual speed / stall | Not measured or detected | Encoder-derived velocity and following-error checks |
| Current regulation | Not implemented | Not implemented; current FOC is a later milestone |
| Motion source | Analog only | Analog or selected serial/Ethernet position source |

The simpler commutation path in Core/Src/open_loop.c performs:

```text
speed_reference approaches target at 10 RPM/s
on opposite-sign request: ramp to zero, inhibit/coast, repeat wake/alignment
electrical_angle = wrap(electrical_angle + 11 * speed_reference_rad_s * 0.001 s)
phase_duties = sine_modulation(electrical_angle, bounded_voltage_vector)
```

The generated stator field rotates in either direction by signed angle accumulation. The rotor follows only if voltage, alignment, acceleration and load allow synchronization. This bypasses encoder calibration, mechanical zero, position P and velocity PI; it retains ADC freshness, six-PWM generation, deadtime, wake/fault logic, stop interlocks and watchdog. It does not provide position holding or regulated torque. [SimpleFOC open-loop velocity explanation](https://docs.simplefoc.com/velocity_openloop).

## State and protection ownership

Core/Src/demo_app.c owns the physical RUN request and neutral dwell. A boot-high RUN is inhibited until stable LOW then HIGH. A valid HIGH request latches a wait; 200 ms continuously valid center input arms OPEN_IDLE with gates off. RUN LOW, stop or fault cancels waiting. Yellow LD2 blinks while waiting and stays steady when armed.

Core/Src/open_loop.c owns OPEN_IDLE -> OPEN_WAKE (10 ms) -> OPEN_ALIGN (2 s) -> OPEN_RUNNING. Center or reversal ramps speed to zero, then OPEN_COAST disables drive for 1 s before returning to idle. Opposite sign during alignment goes directly to coast. Physical standstill is not measured; the pause must be bench-tested. Board hardware owns complementary PWM, break inhibition and output authorization. The diagnostic PWM scope profile is separate and retains its unipolar duty mapping / 100 mV off threshold.

Status fields cmd_mrpm/ramp_mrpm are signed commanded/generated speed in milli-RPM, not shaft measurements; open_dir is the current field direction and wait_neutral reports the physical pending request. No position validity is claimed in open loop.

## Defaults and assumptions

The current map has equal 1.3 V active spans and +/-100 RPM full scale. Center 1.3–1.7 V is inclusive; sensitivity is 76.923 RPM/V. Acceleration/deceleration is 10 RPM/s; alignment is 1 V with a 0.5 s ramp within 2 s; run vector is 2 V with 2 V/s slew. Bus remains configured 24 V, not measured. These are bench limits, not motor ratings/current limits. Check actual voltage/current/temperature and rotor synchronization. **Pot minimum now commands reverse; bias an unplugged PA3 to center instead of using the old ground pull-down.** See the board-control guide for divider/loading details.

## Build and optional console control

Normal outputs are build/Debug and build/Release; native CubeIDE uses Debug/ and Release/. Explicit --profile open-loop or --profile closed-loop overrides the header and writes build/OpenLoop or build/ClosedLoop. It does not override DEMO_OPEN_BOARD_CONTROL; default open-loop builds still use physical RUN. --scope-test on writes build/ScopeTest and forces ENABLE LOW. Build commands do not flash hardware.

For the optional serial-controlled path set DEMO_OPEN_BOARD_CONTROL=0 before rebuilding. ST-LINK VCP is 115200 8N1. While inhibited, MODE OPEN_LOOP selects this controller; with PA3 in 1.3–1.7 V, ARM enters OPEN_IDLE. This legacy path does not latch a wait-for-neutral request or require the physical-profile dwell; ARM outside center remains disabled. DISABLE or ! immediately inhibits via the existing board stop. SOURCE SERIAL/ETHERNET motion and CALIBRATE/ZERO are rejected in open loop. Disable before switching to MODE CLOSED_LOOP; new calibration/ZERO are required before closed-loop ARM.

## Verification

Tests exercise the actual portable controller and application, including both signs and angle wrap, neutral mapping/ARM, boot-high inhibit, pending request/dwell reset, RUN/stop cancellation, coast/re-alignment, fault latching, invalid/stale ADC and unsigned time wrap. Closed-loop, protocol/client and diagnostic scope regression checks remain. See [VALIDATION.md](VALIDATION.md) and [build_evidence.json](build_evidence.json) for final software results; software checks do not establish physical standstill or regulated motor current.
