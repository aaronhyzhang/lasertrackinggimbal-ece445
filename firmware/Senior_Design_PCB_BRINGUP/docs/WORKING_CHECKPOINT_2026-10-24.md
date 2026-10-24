# Working J4 motor checkpoint, 2026-10-24

Saved on branch `pcb-bringup-h743`, tag `working-j4-openloop-2026-10-24`.
The user reports motor rotation after repairing the J4 phase-B high-side
gate-net copper short to GND. This checkpoint preserves the active CubeIDE
source and the user's latest tuning; it does not change motor-control behavior.

## Current defaults

| Setting | Value |
| --- | --- |
| Stage / axis | `PCB_MOTOR_RUN` / `1` (J4, TIM8) |
| Maximum signed field-speed command | +/-150 RPM mechanical equivalent, 11 pole pairs |
| Running phase-voltage amplitude | 2.5 V |
| Acceleration limit | 10 RPM/s |
| Nominal DC bus / PWM carrier | 24 V / 20 kHz |
| Alignment | 1 V, 2 s, 500 ms ramp |
| Running voltage slew | 2 V/s |
| High-side gate source / sink | 15 / 30 mA |
| VDS threshold | 150 mV, baseline latched protection |
| Sync-start / CPUV-history acceptance / fault-capture experiments | All off |

The repository now matches the active MOTOR_RUN/J4 selection. This supersedes
older documentation describing repository/archive defaults as IO_CHECK/J3 or
output verification as pending. For an initial I/O-only check, explicitly select
`PCB_IO_CHECK` before building. S1 and neutral qualification are still required
to arm; S2 inhibits PWM. No automatic arming or fault retry was added.

J3 and J4 share motor tuning and the baseline driver profile. Expected healthy
register values at addresses 0..6 are `[0, 0, 128, 768, 768, 768, 640]`.
Necessary timer/pin and fault-route differences remain: J3 uses TIM1 hardware
break; J4 uses TIM8 with shared nFAULT falling-edge EXTI shutdown and the driver's
own protection. Both use the shared SPI2 bus.

The command is an open-loop rotating-field speed, not measured rotor RPM.
The agent has not flashed or operated hardware, measured phase current, or
qualified stable operation at the maximum command. Earlier user-reported
skipping at a higher setting remains relevant tuning history.

## Software verification

- Active project CubeIDE clean Debug and Release builds: 0 errors, 0 warnings.
- Repository host suite: 30 test executables and 12 invalid-config compile guards passed.
- Core source, driver source, tools and tests match the active project byte for byte.
- The motor test settling horizon now derives from the configured speed/ramp,
  retaining its steady-speed assertions with the longer 150 RPM ramp.

Only a machine-derived Eclipse language-cache hash was excluded from the source
mirror. Build products and machine-local launch files are not committed.
See `working_checkpoint_evidence.json` for source and verification-log hashes.
