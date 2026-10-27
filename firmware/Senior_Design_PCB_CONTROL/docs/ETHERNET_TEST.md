# Ethernet bench test

For faster tuning iterations, use `py -3 .\Tools\position_sweep.py --axis 0 --tune`.
Both tuning and ordinary sweeps now save CSV/JSON and print one suggested change
after completion/failure. See [logged position tuning](TUNING.md) for metrics,
comparison rules and limitations. This script-only update needs no reflash.

## Remote electrical calibration

The CALIBRATE command requires the updated CONTROL firmware, motion enabled and
CONTROL_SOURCE_ETHERNET=1. It is accepted only while stopped in OFF or READY,
not while running, calibrating or faulted. The script starts motor alignment
and the short electrical calibration sweep immediately; no S1 press is needed.
Keep the shaft clear and away from a hard stop, as with S2 calibration.

```powershell
py -3 .\Tools\calibrate_motor.py --axis 0
```

The client maintains heartbeats, waits for calibration to enter its active
states, then requires calibrated READY before reporting PASS. It sends DISABLE
on completion/error/Ctrl-C. S2 stops immediately; lost heartbeats trip after
500 ms. Commands use the same owner/session/sequence checks as POSITION.
Calibration invalidates old mechanical zero. By default the updated script
prompts you to place the stopped shaft at the physical zero, release it and
press Enter. It sends ZERO, accepted only in calibrated READY, and verifies
zero=true with measured position near zero. No PWM is enabled by ZERO and no
position command is armed. Use --skip-zero for electrical calibration only;
physical S2 long-press/release remains available. Updated firmware is required
for the ZERO protocol command. It shares session/owner/replay checks.

The saved checkpoint uses pot input, voltage commutation, and motion enabled.
User tuning at this checkpoint: 550 RPM, 200 RPM/s, RUN_VOLTS=6, VOLT_LIMIT=6.
These are saved settings, not validated system limits. The duty cap still limits
applied voltage to bus_v*0.15 (about 3.6 V at 24 V).

## 1. Link and status, no motion command

Keep the controller stopped. Connect its Ethernet jack directly to the laptop
or through an Ethernet switch. Set the laptop Ethernet adapter's IPv4 address
to 192.168.10.1, subnet mask 255.255.255.0; no gateway or DNS is needed for this
isolated connection. Board is static 192.168.10.2, UDP port 5005. Avoid another
adapter/VPN using the same subnet. Use an appropriate Python/private-network
firewall allowance if Windows prompts; do not disable the firewall wholesale.

From the CONTROL project directory on the laptop, run (Python 3, standard library
only; no pip packages):

```powershell
py -3 .\Tools\position_client.py --host 192.168.10.2
```

This sends STATUS only and works with pot-source firmware too. It does not arm
or move the motor. Expect a printed dictionary containing axis, state, fault,
position_mdeg and source. Source 0 means pot; 1 means Ethernet.

Live Expressions: g_network.state should be NETWORK_LINK_UP, phy_address=0,
phy_id1=7, phy_id2 masked by 0xfff0=0xc130. rx_frames and tx_frames should
increase during queries. A status reply validates PHY, RMII/MAC, ARP/IP/UDP and
the application round trip. rx_commands counts parsed commands, including STATUS;
it should also increase. Link LEDs alone do not establish a packet round trip.

If it times out: check LINK_UP, laptop address/subnet and cable first. If link
is up but rx_frames stays zero, inspect laptop routing/firewall and destination.
If rx_frames rises without a reply, inspect rejected/tx_frames and packet capture.
Ping is supplementary; the script's STATUS response is the required test.

## 2. Select Ethernet and validate motion

In Core/Inc/control_config.h set CONTROL_SOURCE_ETHERNET=1, retain
CONTROL_MOTION_ENABLED=1 and CONTROL_CURRENT_PI_ENABLED=0. Rebuild and flash
CONTROL Debug. The --axis argument checks the selected firmware axis; it does
not switch hardware outputs. Axis 0 is J3; axis 1 is J4.

For the first Ethernet motion use conservative tuning already commissioned:
CONTROL_MAX_RPM=30, CONTROL_ACCEL_RPM_S=30, CONTROL_RUN_VOLTS=2.5,
CONTROL_VOLT_LIMIT=3.0. Do not combine first-network commissioning with an
unqualified speed increase. The saved checkpoint retains the user's tuning.

Repeat S2 electrical calibration from stopped (hold >=2 s, release), wait for
SERVO_READY, place the shaft at the desired physical zero with PWM off, then
hold/release S2 again for mechanical zero. Confirm calibrated=true, zero=true,
fault=0 and state=5 (READY). Do not press S1 yet.

```powershell
py -3 .\Tools\position_client.py --host 192.168.10.2 --axis 0 --position 10 --timeout 30
```

After the script prints "Command accepted. Press physical S1 to arm", press S1.
The script sends heartbeats, reports measured position and RPM, and requires
RUNNING plus settled encoder position within 2.5 degrees of the requested angle.
It prints PASS then sends DISABLE, releasing torque. Repeat with 20 and 40 degrees
before larger moves. An ACK alone is not a positioning pass. Keep targets away
from the 0/360 endpoints; applied targets clamp to 2..358 degrees, no wrap.

## 3. Stop and disconnect behavior

With motion established, test S2 immediate stop. Restart a test from READY.
Then test Ctrl-C (client sends DISABLE), cable unplug and abrupt client loss.
Lost heartbeats must inhibit after the 500-ms lease expires; reconnect must not
automatically rearm. A latched fault needs reset and repeat calibration/zero.
Keep the supply shutoff accessible. Record status, final position, faults and
network counters for each test. Hardware Ethernet and closed-loop motion are
not marked validated until these board tests pass.

## 4. One-arm sweep sequence

From READY with electrical calibration and mechanical zero complete:

```powershell
py -3 .\Tools\position_sweep.py --axis 0
```

The script queues 2 degrees, prompts for S1, and maintains one command session
through 2->358->2, six random moves, then 2->358->2. It checks the echoed target
and measured position within 2.5 degrees for 0.5 seconds, with at most 1 degree
peak-to-peak movement and absolute estimated velocity <=2 RPM throughout.
It does not require the firmware settled flag, whose tighter 0.5-RPM condition
may chatter at rest. It never wraps across
the boundary or automatically rearms after a stop. Final completion, timeout,
fault, Ctrl-C and errors attempt DISABLE. The board's command lease remains active.

For a smaller first trial use --min-deg 20 --max-deg 60. Change --random-moves 6,
--seed 1 or --move-timeout 30 as needed; --arm-timeout defaults to 60 seconds.
The seed makes the random sequence repeatable. No firmware changes/reflash are
needed if the installed image already uses Ethernet source. Host UDP regression
tests cover sequencing, bounds, settling timeout and stop cancellation; physical
sweep performance remains to be tested.
