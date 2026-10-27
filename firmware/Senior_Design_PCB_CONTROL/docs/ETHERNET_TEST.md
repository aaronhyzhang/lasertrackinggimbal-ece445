# Ethernet bench test

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
