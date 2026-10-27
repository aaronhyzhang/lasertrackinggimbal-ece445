# Host: laptop software (camera → PCB)

Everything that runs on the laptop for the tracking gimbal: calibration, camera, ball
detection, the bridge that commands the PCB over Ethernet, and R&V measurements. It talks
to `firmware/Senior_Design_PCB_CONTROL` (dual-axis build) using its `G1` UDP protocol.

```text
USB camera ─► detector ─► pixel → angle ─► bridge ─► Ethernet (UDP) ─► PCB firmware ─► pan / tilt
            (vision.py)  (camera_model.py) (bridge.py)                  (Colin's + cal/FSM)
```

- **One command:** `py -3 -m gimbal_bridge.run_gimbal --config gimbal_config.json`
- Python 3. The bridge, calibration and R&V tools use the standard library only; camera and
  detection need `numpy` and `opencv-python` (YOLO is optional, see Detector).
- Tested against a fake board and a synthetic camera. **Not yet run on the real PCB.**

All commands below are run from this `host/` folder.

---

## Contents

| File | What it does |
|---|---|
| `gimbal_bridge/run_gimbal.py` | **The launcher.** Calibrate if needed → camera + detector → S1 → track → clean stop |
| `gimbal_bridge/calibration.py` | Brings both axes to calibrated + zeroed READY (uses what the board restored from flash; homes pan; asks for a hand zero on tilt) |
| `gimbal_bridge/bridge.py` | 50 Hz loop: vision error → absolute targets, heartbeats, TARGET reports, status, safety stops, CSV log |
| `gimbal_bridge/vision.py` | Camera → detector → angles → bridge, with timing stats |
| `gimbal_bridge/detector.py` | Ball detectors: HSV colour + circularity (default), optional YOLO |
| `gimbal_bridge/camera_model.py` | Camera intrinsics, lens-distortion removal, pixel → angle |
| `gimbal_bridge/camera.py` | USB camera reader (newest frame, capture timestamp) and a synthetic camera |
| `gimbal_bridge/camcal.py` | Camera calibration with an OpenCV checkerboard → `camera.json` |
| `gimbal_bridge/tracking.py` | Target math (gain, deadband, step limit, window clamp) |
| `gimbal_bridge/g1.py` | G1 protocol client |
| `gimbal_bridge/track.py` | Bridge-only runner (synthetic ball or UDP vision input), no camera code |
| `gimbal_bridge/rv.py` | Communications R&V: latency, throughput, bad-packet rejection |
| `gimbal_bridge/fake_board.py` | Fake PCB on localhost, including calibration, HOME, FORGET and power cycles |
| `gimbal_config.example.json` | Every setting, with defaults. Copy to `gimbal_config.json` and edit |
| `tests/` | 39 tests (bridge, calibration, vision, launcher) |

Axis map (firmware): **tilt = axis 0 = J3 = UDP 5005**, **pan = axis 1 = J4 = UDP 5006**.

---

## Try everything without hardware

Terminal 1, a fake board that starts uncalibrated (Enter = S1, `s` = S2, `p` = power cycle):

```bash
py -3 -m gimbal_bridge.fake_board --uncalibrated
```

Terminal 2, the full system with a rendered ball instead of a camera:

```bash
py -3 -m gimbal_bridge.run_gimbal --host 127.0.0.1 --synthetic
```

It calibrates tilt (press Enter when asked for the hand zero), asks for S1 to home pan
(press Enter in terminal 1), then asks for S1 again to start tracking. Ctrl-C stops.
Press `p` in terminal 1 and run again: pan comes back already zeroed from the "flash".

Tests:

```bash
py -3 -m unittest discover -s tests -v
```

---

## Running on the real gimbal

### One-time setup
1. Flash the **dual-axis** build (`CONTROL_DUAL_AXIS=1`, Ethernet source, motion on). See
   `firmware/Senior_Design_PCB_CONTROL/docs/DUAL_AXIS.md`.
2. Laptop Ethernet adapter: **192.168.10.1 / 255.255.255.0**, no gateway (board is 192.168.10.2).
3. `py -3 -m pip install numpy opencv-python`
4. Calibrate the camera (below) to get `camera.json`.
5. Copy `gimbal_config.example.json` to `gimbal_config.json`. Set at least the **tilt
   limits** (`tracking.tilt_limits`) to the real safe range and the **ball colour** (`detector`).

### Every run

```bash
py -3 -m gimbal_bridge.run_gimbal --config gimbal_config.json --preview
```

1. Connects to both axes and prints what the board restored from flash.
2. Calibrates only what is missing:
   - not electrically calibrated → CALIBRATE (shaft aligns, sweeps ~8 degrees, returns)
   - pan without zero → **press S1**: pan homes slowly to its clockwise hard stop (= 0 degrees)
   - tilt without zero → move it by hand to its 0-degree mark, press Enter
3. Opens the camera and detector (fails here, before anything moves, if the camera is missing).
4. **Press S1**: the gimbal moves to the start pose (centre of the tracking windows by default,
   `start_pose` in the config), then follows the ball.
5. Ctrl-C (or `q` in the preview window) → DISABLE. **S2** on the board stops everything at any time.

After the first full calibration, a power cycle normally needs only the tilt hand zero (pan's
zero is restored; see Calibration).

---

## Calibration

The board stores calibration in flash with a checksum and restores it at boot
(`firmware/.../docs/CALIBRATION_STORE.md`). `calibration.py` uses that, and only does what is missing.

| Situation after power-up | What happens |
|---|---|
| Fresh board | Tilt: calibrate + hand zero. Pan: calibrate + HOME (S1) |
| Calibrated before | Pan: restored (nothing to do). Tilt: hand zero only (its zero is not restored until its mechanical travel is confirmed under one turn) |
| Magnet/encoder remounted | Run with `--forget` first, then calibrate again |

Standalone: `py -3 -m gimbal_bridge.calibration` (add `--forget` or `--no-home`).

## Camera calibration

Print a checkerboard and count its **inner** corners (a 10x7-square board has 9x6).

```bash
py -3 -m gimbal_bridge.camcal --camera 0 --board 9x6 --square-mm 25 --out camera.json
```

SPACE saves a view when the corners are found (15–25 views at different distances, tilts and
image corners), ENTER calibrates, ESC quits. RMS under ~0.5 px is good. Calibrate at the
tracking resolution. From photos instead: `--images calib/*.png`.

## Detector

Default `color`: HSV threshold + roundness. Tune `hsv_ranges` (OpenCV HSV, H 0–179) to the ball;
use `--preview` to check. Red wraps around H=0, so give two ranges. `min_area` (px) and
`min_circularity` (0–1) reject noise and non-round blobs.

`yolo`: `py -3 -m pip install ultralytics`, then
`"detector": {"type": "yolo", "model": "yolo11n.pt", "classes": ["sports ball"], "conf": 0.35}`.
Any other detector only needs a `detect(frame) -> Detection | None` method.

---

## How tracking works

The detector's ball centre becomes an angular error from the image centre (right of centre and
above are positive), corrected for lens distortion, plus `aim_offset_deg`. Each new frame, per axis:

```text
target = clamp( measured_position + sign × gain × error , axis window )
```

The correction is added to the **measured** motor position. Errors under the deadband are
ignored; one frame moves a target at most `max_step_deg`. The bridge also sends `TARGET 1/0`
so the board's own state machine shows TRACKING / SEARCHING.

| Setting | Default | Meaning |
|---|---|---|
| `tracking.pan_sign` / `tilt_sign` | -1 / +1 | Direction of correction. **Confirm on the bench** |
| `tracking.gain` | 0.8 | Fraction of the error corrected per frame |
| `tracking.deadband_deg` | 0.5 | Ignore smaller errors |
| `tracking.max_step_deg` | 20 | Largest correction from one frame |
| `tracking.tilt_limits`, `pan_limits` | 15–345, 15–320 | Allowed target window. **Narrow tilt to the real range** |
| `tracking.stale_s` | 0.3 | Newest frame older than this = ball lost |
| `start_pose` | `"center"` | Where S1 first sends the gimbal; `{"tilt": .., "pan": ..}`, or `null` to stay put |
| `aim_offset_deg` | 0, 0 | Fixed correction for where the gimbal should aim relative to the camera centre |
| `camera.intrinsics` | `camera.json` | From `camcal`; if missing, a field-of-view estimate is used with a warning |

## Safety behaviour

The firmware's protections always apply. On top of them:

| Situation | What happens |
|---|---|
| Start | Refuses unless both axes are calibrated, zeroed, READY and fault-free |
| Arming | **Physical S1 only.** HOME also needs S1. No network command arms |
| Before S1 | Targets are the current position, or the start pose (moved to at the firmware's speed limits) |
| Ball lost / frames stale | SEARCHING: hold the last target, keep the 500 ms lease alive |
| Fault, refused command, S2, lost link | DISABLE both axes and stop |
| Laptop crash or cable pulled | The firmware's own 500 ms lease stops the motors |
| Homing stalls somewhere other than the known stop | Board faults instead of trusting the zero |

> DISABLE releases holding torque. A loaded tilt axis can drop; support it.

## Communications R&V (design doc 2.3.3)

With both axes stopped: `py -3 -m gimbal_bridge.rv all --port 5005` (and `--port 5006`).
Latency (one-way < 5 ms for 99 %), throughput (≥ 1 Mbps), 20 malformed packets rejected.
Results in `logs/rv/`. `run_gimbal` also prints the laptop-side pipeline time
(capture → command) and detector time, and logs every cycle to `logs/run/`.

## Using the bridge from your own code

```python
from gimbal_bridge import Bridge, Observation, TrackerConfig
from gimbal_bridge.camera_model import Intrinsics

k = Intrinsics.load('camera.json')
bridge = Bridge(TrackerConfig(tilt_limits=(60, 120)))
bridge.start(on_waiting_for_s1=lambda: print('Press S1'), start_pose=(90, 160))
...
pan_err, tilt_err = k.angles(ball_u, ball_v)                     # per frame
bridge.submit(Observation(True, pan_err, tilt_err, t_capture))   # or Observation(False)
...
bridge.close()                                                   # DISABLE
```

Or send JSON to `track.py --input udp` on `127.0.0.1:5100`:
`{"detected": true, "pan_error_deg": 1.5, "tilt_error_deg": -0.4}`.

## Troubleshooting

| Message | Likely cause / fix |
|---|---|
| `not the dual Ethernet motion build` | Single-axis or pot-source firmware is flashed |
| `fault ...` | Fix the cause, reset the board; `CALIBRATION` after HOME = false stall or changed mechanics |
| `S1 was not pressed` | Press S1 within the timeout (HOME: 60 s, tracking: `arm_timeout_s`) |
| `outside tracking window` | Use `start_pose` (default) or move the axis inside the window |
| `cannot open camera` | Wrong `camera.source` index, or another program holds the camera |
| `camera.json not found` warning | Run `camcal`; aiming is approximate until then |
| `refused a command` | Lease expired, S2 pressed, or the other axis stopped |
| `replies missed; link lost` | Cable, laptop IP 192.168.10.1/24, firewall |
| Ball found but gimbal runs away | A `*_sign` is wrong: S2, flip it |

## Tests

```bash
py -3 -m unittest discover -s tests -v
```

39 tests: tracking math, G1 client, bridge (arming, tracking, holding, stops, lease, TARGET,
older firmware), calibration (fresh board, restore after power cycle, faults, missed S1,
FORGET, older firmware), vision (detector, distortion, camera calibration recovering known
intrinsics within 2 %, closed loop with the synthetic camera), launcher end to end, R&V tools.

They use `fake_board.py`, which copies the firmware's protocol and calibration rules but not
its motor dynamics, timing or electrical behaviour. **Passing them is not hardware validation.**

## Still to do on hardware

- Confirm pan/tilt signs, tilt limits, start pose and detector colours on the real gimbal.
- Confirm tilt mechanical travel; if under one turn, enable its zero restore in firmware.
- Measure end-to-end latency (camera capture to motion) for the R&V table.
- Determine `aim_offset_deg` if the aim point differs from the camera centre.
