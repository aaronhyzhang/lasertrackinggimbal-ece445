# PWM, gate-driver and first-spin validation

**v0.4.1 wiring change:** INLB now comes from **PE10 / CN12 pin 47**, replacing PB0 / CN11 pin 34. Move that wire with power off before energizing the driver. See [phase-B diagnostics](PHASE_B_PWM_DIAGNOSTIC.md).

Current v0.4.1 default: **open-loop motor control using PG2 RUN and the PA3 pot**, with no serial commands, encoder or Ethernet required. Scope-test mode is now off. See [OPEN_LOOP_BOARD_CONTROL.md](OPEN_LOOP_BOARD_CONTROL.md) for wiring, low-to-high RUN/zero-speed arming and fault reset.

Updated 2026-10-13. Firmware v0.2.2. All figures below are configured expectations, not scope measurements. Record measured results in [VALIDATION.md](VALIDATION.md). Wiring and EVM straps are in [BOARD_CONFIG.md](BOARD_CONFIG.md).

## Change one line before flashing

In `Core/Inc/demo_config.h`:

```c
#define DEMO_DEFAULT_OPEN_LOOP 1 /* 1=open-loop speed, 0=closed-loop position */
```

Use an ordinary CubeIDE rebuild or `python Tools/build.py --config Debug`; both follow this header. No IDE preprocessor change or serial MODE command is needed to choose the boot mode. A firmware change still requires rebuild and flash. Set 0 and rebuild for the existing position controller. Both boot disabled and still require ARM; closed loop additionally requires calibration/ZERO.

Explicit CLI `--profile open-loop` or `--profile closed-loop` is an advanced override and emits separate images under `build/OpenLoop/<config>` or `build/ClosedLoop/<config>`. Normal builds use `build/<config>` and the header. Avoid explicit overrides when you intend the one-line setting to be authoritative. Native CubeIDE artifacts remain `Debug/` and `Release/`; confirm `STATUS mode=` and `fw=` after flashing the intended image.

## Sequence and electrical references

Yes: **MCU alone -> powered EVM with motor disconnected -> unloaded motor**. Verify shutdown at each stage, not just whether a waveform exists. Connect or change winding/harness/probe connections with power off. Flash/debug with motor bus disconnected; do not pause the CPU while driving.

| Stage | Probe point / reference | Main pass evidence |
| --- | --- | --- |
| 1, Nucleo only | Each MCU PWM pin to Nucleo GND | 3.3 V logic, carrier, complementary pairing, non-overlap, preload envelope, inhibit |
| 2, EVM inputs | INHx/INLx to EVM signal GND | Same logic signals arrive at the correct phases; MODE really is 6-PWM |
| 2, MOSFET gates | High side: GHx minus SHx; low side: gate minus its actual source | Correct VGS levels, no same-leg simultaneous enhancement, off states and fault shutdown |
| 2, power stage | SHx to power GND, with a suitable voltage-rated setup | Rail-to-rail switching, ringing/overshoot and duty-envelope response |
| 3, unloaded motor | Repeat gate/node checks; bus/phase current with appropriate equipment | Stable alignment/spin, measured speed/direction/current/temp and stop behavior |

On an ordinary bench scope, channel ground clips are typically common and earth-referenced. **Do not attach a standard probe ground clip to SHx/the floating high-side source.** Use a suitably rated differential or isolated probe for GHx–SHx, including adequate common-mode range and transient margin. A high-side gate-to-ground trace alone is not VGS. For low-side VGS, account for the source/shunt connection rather than assuming source equals signal GND exactly. Compensate probes, use short leads and deskew channels when comparing gate timing. If the appropriate floating measurement setup is unavailable, limit the initial checks to ground-referenced MCU/input signals and properly referenced low-side measurements until it is available. Never defeat the scope's protective earth to make a floating measurement.

## Stage 1 — Nucleo PWM, no EVM or motor

1. Select open loop in the header, rebuild, flash with the EVM disconnected, and open VCP at 115200 8N1. STATUS should show OPEN_LOOP, DISABLED, firmware 0.2.2. ENABLE/PG4 must remain low at boot.
2. Set analog input to its minimum (approximately 40 mV on the reported pot, within the 100 mV deadband). Meter/check the ADC at 0/0.75/1.5/2.25/3 V. The deadband is 100 mV. ARM at zero should give OPEN_IDLE with **all six PWM outputs inactive**. It must not start automatically at reset.
3. For this MCU-only test, hold PE6/nFAULT high with an appropriate pull-up resistor to 3.3 V (about 4.7 kΩ; the BSP also requests an internal pull-up). This is a test fixture for the absent driver, not a bypass to leave in place against a real fault output. Do not tie nFAULT directly to the supply. A real EVM's open-drain fault output must still be able to pull the line low.
4. Issue ARM with input at zero, then raise the analog command. After 10 ms wake and the 2 s alignment stage, the generated field rotates. Check each pair below at roughly 50–100 ns/div for edge timing, then about 10 µs/div for the carrier. Use appropriate trigger levels near the logic thresholds and capture both edges.

| Phase | High input / Nucleo pin | Low input / Nucleo pin |
| --- | --- | --- |
| A | TIM1_CH1 /PE9 | TIM1_CH1N /PE8 |
| B | TIM1_CH2 /PE11 | TIM1_CH2N /PE10 |
| C | TIM1_CH3 /PE13 | TIM1_CH3N /PE12 |

Expected while driving:

- All channels use the same center-aligned **20 kHz carrier**, nominal period **50 µs**. These are synchronized timer channels; their carriers are not intentionally staggered by 120°.
- Each high/low pair alternates, with a **both-low interval** at transitions. Firmware requests **500 ns** MCU deadtime (50 ×10 ns at the configured 100 MHz timer clock). Neither input should be high while its partner is high. This restriction is within the same half-bridge: high-side switches from different phases may be on together normally.
- At a stationary alignment angle, duties are stationary; B and C may be equal. Equal B/C duties at that one angle are not evidence of a wiring fault. With a 1 V generated vector on a configured 24 V bus, nominal pre-deadtime duties are approximately A=54.17%, B=C=47.92%.
- Once rotating, the **slow duty envelopes** are sinusoidal with 120° electrical phase separation, while the 20 kHz carrier remains synchronized. At 30 RPM with 11 pole pairs, the envelope is **5.5 Hz**, period approximately **181.8 ms**; 120° corresponds to approximately **60.6 ms** at that steady speed. Use duty-cycle trend measurements or scope low-pass/averaging math to see the envelope; a zoomed-out raw digital trace may just look like a dense pulse band. Do not filter the actual signals feeding the gate driver. Raw PWM edges are not spaced by 60.6 ms.
- At 2 V running vector magnitude on 24 V, ideal duty range is about **41.67–58.33%** before deadtime/timer quantization. This is not a 0–100% sweep or a rated torque command.

Force PE6 low through a suitable fixture and verify all outputs inhibit; while active it should latch DRIVER. DISABLE and `!` should inhibit PWM and lower ENABLE. Reset should remain disabled. Verify both states when the analog input is held nonzero at reset or ARM: no automatic start and ARM refusal outside the zero region. Record actual high/low logic levels, frequency, deadtime and shutdown captures before moving on.

## Stage 2 — powered gate-driver EVM, no motor

1. Power off. Connect the verified six-PWM/ENABLE/nFAULT/common-ground harness, establish VDDIO=3.3 V, retain an effective ENABLE pull-down, and verify the fault pull-up allows low assertion. The **H** driver uses hardware straps: MODE=AGND for 6-PWM, CAL low for this profile; record GAIN/IDRIVE/VDS settings. Software OPEN_LOOP versus CLOSED_LOOP does not change the DRV8304 hardware MODE strap.
2. Leave all three winding leads disconnected. Apply the measured, current-limited 24 V bus and check idle consumption/rails, ENABLE low and both MOSFETs off in each leg. Start with a low current limit sufficient for board startup; supply current limiting/collapse can cause undervoltage faults, so distinguish that from a gate-timing problem.
3. Repeat the VCP/analog sequence and first scope the logic signals **at the EVM inputs**, confirming A high is actually INHA, A low is INLA, etc. In 6-PWM, both inputs low or both inputs high request high impedance; INH=1/INL=0 drives high and INH=0/INL=1 drives low. We deliberately use complementary inputs with a 00 gap, not reliance on the invalid/both-high condition.
4. Probe gate-to-source voltages on one leg at a time, then all three. With 24 V bus, the datasheet's typical gate drive is around **10 V VGS**, not 3.3 V. High-side gate-to-ground can reach approximately bus + gate drive; evaluate VGS and probe common-mode ratings accordingly. Compare levels/ringing to the actual MOSFET's ratings and driver conditions.
5. Confirm one MOSFET enters its off region before the other enters conduction; inspect both transition directions. The driver adds its VGS handshaking and internal deadtime, so gate-waveform deadtime need not equal precisely 500 ns or simply 500+120 ns. Measure actual non-overlap instead of adding nominal numbers. Check for false gate turn-on, excessive ringing, repeated faults and unexpected bus current.
6. Check phase nodes SHx against power GND. They switch approximately **0–24 V**, even though firmware vector effort is limited to 2 V: PWM duty differences establish average phase-to-phase voltage. During off/deadtime an unloaded node may float or ring. A motor-disconnected node is not a loaded motor-voltage or current measurement.
7. Verify driver nFAULT shutdown, `!`, DISABLE, reset and supply transitions at the actual gate pins. Expect transient nFAULT low during ENABLE wake/sleep; PWM remains inhibited during the firmware's 10 ms wake interval. A later active-drive fault must latch and must not automatically restart. Do not proceed until all gates are verified off during inhibition and there is no unexplained current or overlap.

Do not change hardware MODE or any strap while MOSFETs are operating. The driver has shoot-through protection, but its existence is not a substitute for the scope checks.

## Stage 3 — secured unloaded motor

1. Power off; attach phases A/B/C and secure the motor with unrestricted rotation and safe lead routing. Keep the camera/gimbal mechanism and mechanical stops out of this first spin test.
2. Set analog to zero, apply current-limited 24 V, check disabled state, then ARM. Begin with a small nonzero analog command after arming. Verify the alignment movement followed by smooth rotation and the expected physical direction.
3. Increase toward the standard speed points only after lower speeds work. Measure actual shaft RPM independently, bus current and (if possible) phase current. A supply current limit is not a calibrated phase-current limit. Recheck VGS/non-overlap/ringing under motor load; unloaded gate traces alone do not establish loaded switching behavior.
4. Check normal zero-command deceleration/coast separately from immediate DISABLE/`!`. Monitor current/temperature, especially during alignment, very slow running or stall. Stop if it chatters, repeatedly faults or loses synchronization. Investigate phasing, bus collapse, limits and load rather than blindly increasing voltage.
5. Once the power-stage/spin evidence passes, add/validate the AS5600, select closed loop before flash (or MODE while disabled), then perform new CALIBRATE and ZERO before the position test.

## What initializes the switching, and why the rotor can follow

Two independent clocks are involved:

- **PWM switching:** TIM1 creates the complementary 20 kHz carrier and deadtime in hardware. All three compare preloads latch together at a natural carrier update; foreground code does not bit-bang individual gates. This controls averaged three-phase voltages and safe leg transitions.
- **Commutation/field rotation:** the 1 kHz control IRQ changes the three duty values from a generated electrical angle. Its nominal update is `theta_e += pole_pairs * mechanical_speed_rad_s * 0.001`, wrapped to one electrical revolution. The three phase-voltage references are 120° apart in electrical angle.

At a nonzero analog command after ARM, firmware wakes the driver with PWM inhibited for 10 ms, then holds a fixed electrical angle. The vector grows from 0 to 1 V over 0.5 s within the 2 s alignment stage. This attracts the rotor toward a nearby stable magnetic orientation. It does **not** read the encoder or prove that alignment completed.

The generated angle then advances continuously in one direction while speed ramps from zero at 10 RPM/s. Voltage ramps from 1 to 2 V at 2 V/s. Starting from a stationary field and moving it gradually gives the rotor a chance to stay synchronized rather than applying an arbitrary high-frequency sequence to an unknown initial angle. With 11 pole pairs, one mechanical revolution requires 11 electrical revolutions; `f_e = 11 * RPM/60`, so 30 RPM needs 5.5 Hz field rotation.

Complementary switching prevents an unsafe leg condition; it does not by itself guarantee rotation. The rotor spins only if phase order, alignment torque, voltage/acceleration and load permit it to follow the rotating field. Open loop cannot guarantee actual RPM or detect pull-out/stall. Positive electrical sequence has one fixed direction; physical CW/CCW depends on winding assignment and viewing side. Fixed-angle startup may move the shaft briefly in either direction toward the nearest alignment orientation before continuous spin; verify mechanical clearance. Open-loop positive field advance does not guarantee a strictly one-way initial alignment movement.

## References

- [TI SPRABZ0A, section 9](https://www.ti.com/lit/an/sprabz0a/sprabz0a.pdf): staged target/PWM checks with motor disconnected before connecting it for unloaded tests.
- [DRV8304 datasheet, sections 6.5, 7.3.1.1 and 7.4.1](https://www.ti.com/lit/ds/symlink/drv8304.pdf): 6-PWM truth table, VGS/deadtime, wake/fault behavior and safe hardware configuration.
- [BOOSTXL EVM guide](https://www.ti.com/lit/ug/slvub97/slvub97.pdf): connection and gate/source headers; verify actual received board revision.
- [Tektronix probe primer](https://www.tek.com/en/documents/whitepaper/abcs-probes-primer): floating/differential measurements, including high-side inverter gate measurements; equipment ratings and correct references matter.
