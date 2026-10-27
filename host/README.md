# Host: laptop bridge (vision → PCB)

Laptop-side software for the laser tracking gimbal. It connects the vision pipeline to the
custom PCB running `firmware/Senior_Design_PCB_CONTROL` (dual-axis build) over Ethernet.

```text
camera ──► vision code ──► host bridge ──► Ethernet (UDP) ──► PCB firmware ──► pan / tilt motors
           (finds ball)     (this folder)                        (Colin's)
```

- Uses the firmware's existing `G1` protocol. **No firmware changes are needed.**
- Python 3 standard library only. Nothing to `pip install`.
- Tested against a fake board on localhost. **It has not been run on the real PCB yet.**

---

## Contents

| File | What it does |
|---|---|
| `gimbal_bridge/g1.py` | **The messenger.** Sends `G1` commands (`POSITION`, `HEARTBEAT`, `STATUS`, `DISABLE`) to one axis and parses replies. |
| `gimbal_bridge/tracking.py` | **The math.** Turns "ball is 3° right, 1° up" into new absolute targets. Also `pixel_to_angles()` for the vision code. |
| `gimbal_bridge/bridge.py` | **The driver.** 50 Hz loop: applies targets, sends heartbeats, reads positions, stops everything on trouble, writes a CSV log. |
| `gimbal_bridge/track.py` | **The program you run.** Feeds the bridge a fake ball (`synthetic`) or real vision data (`udp`). |
| `gimbal_bridge/rv.py` | **R&V measurements.** Latency, throughput and bad-packet rejection for the design document. |
| `gimbal_bridge/fake_board.py` | **Pretend PCB** on your laptop for testing without hardware. |
| `tests/test_bridge.py` | 16 automated tests against the fake board. |

Axis map, set by the firmware:

| Axis | Motion | Connector | UDP port | Firmware command range |
|---|---|---|---|---|
| 0 | Tilt | J3 | 5005 | 2–358° |
| 1 | Pan | J4 | 5006 | 2–337° (0° = clockwise hard stop) |

All commands below are run from this `host/` folder.

---

## Quick start: try it without hardware

Terminal 1, the fake board:

```bash
py -3 -m gimbal_bridge.fake_board
```

Terminal 2, the bridge with a fake ball:

```bash
py -3 -m gimbal_bridge.track --host 127.0.0.1
```

When terminal 2 says *Press physical S1*, press **Enter** in terminal 1 (that is the fake S1).
Terminal 2 then prints both axes following the fake ball. Type `s` + Enter in terminal 1 to
simulate S2, or press Ctrl-C in terminal 2 to stop.

Run the tests:

```bash
py -3 -m unittest discover -s tests -v
```

---

## How tracking works

The vision code reports the ball's **angle from the centre of the image**:

| Value | Positive means |
|---|---|
| `pan_error_deg` | ball is **right** of centre |
| `tilt_error_deg` | ball is **above** centre |

Every new frame, for each axis:

```text
target = clamp( measured_position + sign × gain × error , axis window )
```

- The correction is added to the **measured** motor position (where the camera was pointing
  when the frame was taken), not to the previous target.
- Errors smaller than the **deadband** (0.5°) are ignored, to avoid jitter.
- One frame can move a target by at most **max step** (20°).
- Targets are clamped to the **axis window**. Defaults are tilt 15–345° and pan 15–320°,
  Colin's sweep-test values. Set the real ones.
- Target changes under 0.1° are sent as heartbeats instead.

| Setting (`track.py` flag) | Default | Meaning |
|---|---|---|
| `--pan-sign` | `-1` | Pan position increases counter-clockwise, so a ball on the right usually needs a smaller pan angle |
| `--tilt-sign` | `+1` | Depends on how J3 is mounted. **Unconfirmed** |
| `--gain` | `0.8` | Fraction of the error corrected per frame |
| `--deadband` | `0.5` | Degrees |
| `--max-step` | `20` | Degrees per frame |
| `--tilt-min/max`, `--pan-min/max` | 15/345, 15/320 | Allowed target window |
| `--rate` | `50` | Bridge loop rate, Hz |

---

## Safety behaviour

The firmware's own protections always apply. On top of them the bridge does the following:

| Situation | What happens |
|---|---|
| Start | Refuses unless both axes are READY, calibrated, zeroed and inside their windows |
| Before arming | Commands each axis to **where it already is**, so S1 causes no jump |
| Arming | **Physical S1 only.** No network command can arm |
| Ball not detected, or newest frame older than 0.3 s | **SEARCHING**: holds the last target and keeps the motors' command timeout alive |
| Fault, refused command, S2, or 8 missed replies in a row | Sends **DISABLE** to both axes and exits |
| Ctrl-C or normal exit | Sends DISABLE to both axes |
| Laptop program crashes or the cable is pulled | The firmware's **500 ms timeout** stops the motors by itself |

> DISABLE releases holding torque. A loaded tilt axis can drop; support it.

---

## Running on the real PCB

### 1. One-time setup
1. Colin flashes the **dual-axis build** (`CONTROL_DUAL_AXIS=1`, Ethernet source). See
   `firmware/Senior_Design_PCB_CONTROL/docs/DUAL_AXIS.md`.
2. Laptop Ethernet adapter: IPv4 **192.168.10.1**, mask **255.255.255.0**, no gateway.
   The board is **192.168.10.2**. Allow Python through the firewall for this network.

### 2. Every power-up
Calibration is lost on reset (it's stored in RAM only). From `firmware/Senior_Design_PCB_CONTROL`:

```bash
py -3 Tools/calibrate_motor.py --axis 0 --port 5005
py -3 Tools/calibrate_motor.py --axis 1 --port 5006
```

### 3. Communications R&V (motors stopped)

```bash
py -3 -m gimbal_bridge.rv all --port 5005
py -3 -m gimbal_bridge.rv all --port 5006
```

| Test | Design-doc requirement | Method |
|---|---|---|
| `latency` | One-way < 5 ms for 99 % of messages | 2000 STATUS round trips; one-way = round trip / 2 |
| `throughput` | ≥ 1 Mbps | STATUS requests kept in flight for 5 s; reports UDP payload and on-wire Mbps |
| `reject` | Bad messages rejected and never applied | 18 malformed packets must all get `ERR syntax`; target must not change |

Results print as PASS/FAIL and are saved to `logs/rv/*.json`. Also compare the reject count
with `g_network.rejected` in CubeIDE Live Expressions. The tool refuses to run while an
axis is RUNNING unless you pass `--allow-running`.

### 4. First tracking run (synthetic ball, no camera)

```bash
py -3 -m gimbal_bridge.track --input synthetic --amplitude 5 --period 10
```

Press **S1** on the board when asked. Both motors should follow the fake ball smoothly.

- **An axis runs to its limit or oscillates:** press **S2**, then flip that axis's sign
  (`--pan-sign 1` or `--tilt-sign -1`) or lower `--gain`.
- When it is smooth, increase `--amplitude` and add `--dropout 0.2` to test losing and
  re-finding the target.

> The synthetic ball uses the same sign setting as the bridge, so this run checks
> stability and tuning, **not** real camera direction. Confirm direction with the real
> camera: a ball on the right must turn the camera to the right.

Logs go to `logs/track/*.csv`: time, mode, vision errors, target, measured position and
state per axis, and command round-trip time.

---

## Connecting the vision code

### Option A: same Python program

```python
import time
from gimbal_bridge import Bridge, Observation, TrackerConfig, pixel_to_angles

cfg = TrackerConfig(tilt_limits=(60, 120), pan_limits=(15, 320))  # use the real safe limits
bridge = Bridge(cfg)                                                 # board at 192.168.10.2
bridge.start(on_waiting_for_s1=lambda: print('Press S1 on the board'))
try:
    while bridge.running():
        frame = camera.read()
        t_capture = time.monotonic()            # time the frame was grabbed
        box = detect_ball(frame)                # your detector
        if box is None:
            bridge.submit(Observation(False))
        else:
            # fx, fy, cx, cy come from OpenCV camera calibration
            pan_err, tilt_err = pixel_to_angles(box.center_x, box.center_y, fx, fy, cx, cy)
            bridge.submit(Observation(True, pan_err, tilt_err, t_capture))
finally:
    bridge.close()                              # always sends DISABLE
```

### Option B: separate program, any language

Start the bridge listening for vision data:

```bash
py -3 -m gimbal_bridge.track --input udp
```

Then send **one JSON datagram per frame** to `127.0.0.1:5100`:

```json
{"detected": true, "pan_error_deg": 1.5, "tilt_error_deg": -0.4}
```

Send `{"detected": false}` when no ball is found. Optional `"t_capture"` must be a
`time.monotonic()` value from the same machine.

> Camera-to-laser offset is not compensated yet. Add it to the errors before submitting.

---

## Troubleshooting

| Message | Likely cause / fix |
|---|---|
| `needs calibration and mechanical zero` | Board was reset. Rerun `calibrate_motor.py` for that axis |
| `not the dual Ethernet motion build` | Single-axis or pot-source firmware is flashed |
| `state FAULT` / `fault ...` | Read the fault name, fix the cause, reset the board, recalibrate |
| `outside tracking window` | Move the axis inside `--*-min/--*-max` before starting |
| `S1 was not pressed` | Press S1 within `--arm-timeout` (60 s) |
| `refused a command` | Command timeout expired, S2 pressed, or the other axis stopped |
| `replies missed; link lost` | Check cable, laptop IP 192.168.10.1/24, firewall |
| `rv`: `no replies` | Same as above; check that STATUS works with `firmware/.../Tools/position_client.py` |

---

## Limitations / still to do

- Not yet validated on the real PCB; pan and tilt signs are unconfirmed.
- The fake board copies the firmware's protocol rules, not its motor dynamics, timing or
  electrical behaviour. Passing the tests is not hardware validation.
- The latency figure is a round trip halved; it assumes symmetric delay and does not include
  camera or detector time. Measure end-to-end latency separately.
- No camera-to-laser offset or depth compensation yet.
- No system state machine (init/calibrate/idle/search/track/fault) on the board yet; the
  bridge's TRACKING/SEARCHING is laptop-side only.
