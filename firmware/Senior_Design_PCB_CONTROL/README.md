# H743 PCB: encoder-based sinusoidal position control and Ethernet

Development project, 2026-10-26. User reports calibration reaches READY and is
tuning pot-position response; Ethernet hardware validation remains pending.
The separate `Senior_Design_PCB_BRINGUP` project is unchanged and remains the
working open-loop speed-test image. This checkpoint boots inhibited but has
`CONTROL_MOTION_ENABLED=1`, pot source, voltage mode and the user's current tuning
(550 RPM, 200 RPM/s, configured 6 V, duty-capped to ~3.6 V at 24 V). These settings
are not established hardware limits. See [Ethernet test](docs/ETHERNET_TEST.md)
for status-only validation followed by conservative motion testing.

The first active stage is **sinusoidal voltage commutation using encoder angle**.
The position loop drives a velocity loop whose output is bounded voltage, not
current. Current sensing supplies telemetry, calibration checks and shutdown
limits. `CONTROL_CURRENT_PI_ENABLED=0` keeps the prepared d/q current PI path
disabled; selecting current regulation is a separate later commissioning step.

## Initial setup

Import this directory as an existing STM32CubeIDE project. It is self-contained,
targets STM32H743ZIT6 and has Debug/Release configurations. No CubeMX regeneration
is required. Select `PCB_TEST_AXIS` in `Core/Inc/pcb_config.h`: 0=J3/TIM1/I2C1;
1=J4/TIM8/I2C2. Only that bridge can actuate. Both encoders are read independently.
VR1 commands the selected motor; VR2 is telemetry only.

Read [COMMISSIONING.md](docs/COMMISSIONING.md) before enabling motion, and
[ETHERNET_AUDIT.md](docs/ETHERNET_AUDIT.md) for the custom PHY/RMII setup.
The standalone build tool does not flash:

```powershell
python Tools/build.py --config Release
python Tools/build.py --config Release --axis 0 --motion on --source pot
python Tools/build.py --config Release --axis 0 --motion on --source ethernet
```

CLI overrides write separate output subdirectories and do not edit headers.
CubeIDE uses the headers: select `CONTROL_MOTION_ENABLED=1` and
`CONTROL_SOURCE_ETHERNET=0` for the first active pot test, or 1 for the later
Ethernet test. Keep `CONTROL_CURRENT_PI_ENABLED=0` for both. The default
motion-disabled ELF is **not** the active profile. Always check the selected ELF
and `g_app_status.motion_enabled/current_regulation/source/test_axis` after flash.

## What to watch

- `g_app_status.servo`: state, fault, commutation calibration, mechanical zero,
  position, requested/applied target, trajectory, RPM, voltage command and currents.
- `g_telemetry`: compact encoder/analog values and validity flags for both outputs.
- `g_network`: PHY ID/address, link state, frame/command counters.

The selected output's `g_feedback` and `g_telemetry` currents now use injected
samples and report **positive current into the motor winding**, opposite the
SP-to-SN shunt-current convention in the old diagnostic bring-up image. The
unselected output remains a slow diagnostic scan; it is never driven here.
The selected current-valid flag requires a fresh, calibrated low-side sample.
Encoder diagnostic health flags still expose the fixed sensor's weak-field
warning. This commissioning controller explicitly permits weak-but-detected
magnet data while retaining freshness, continuity and plausibility checks.

Calibrations are RAM-only and must be repeated after reset. Short S2 always
inhibits immediately; no packet arms the bridge or clears a fault. Long S2 only
requests an operation if the press began while already inhibited, and the
operation begins on release. Faults require reset and calibration again.

## Laptop validation

Set a laptop Ethernet adapter to `192.168.10.1/24`; board is `192.168.10.2`, UDP
5005. Avoid a conflicting subnet/address. Allow the Python client through the
laptop firewall on this bench connection. This protocol is for an isolated,
trusted bench network and does not authenticate packets.

```powershell
python Tools/position_client.py
# After calibration, zeroing, and selecting an active Ethernet-source build:
python Tools/position_client.py --axis 0 --position 90
```

The client sends POSITION, then asks you to press physical S1. It sends heartbeats
and polls measured encoder position. PASS requires the controller to have entered
RUNNING and settled within 2.5 degrees; an ACK alone does not pass. Completion,
errors, timeout and Ctrl-C send DISABLE. Loss of packets expires a 500 ms lease.
DISABLE inhibits and releases holding torque; support any gravity-loaded mechanism.

## Software validation

```powershell
python Tools/test.py --zig path/to/zig.exe
python Tools/test_bsp.py --zig path/to/zig.exe
```

Portable tests exercise voltage mode, optional future current PI, bounded travel,
calibration in both encoder directions and fault paths. Scripted-HAL tests use
the actual application/acquisition code, and actual LwIP IPv4/UDP, with private
MMIO memory on Windows. Python tests use real localhost UDP sockets. These tests
do not prove ADC settling, PHY electrical operation, loop tuning or motor motion.
