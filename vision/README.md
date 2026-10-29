# Camera calibration and ball tracking

Run these commands from the project root after activating your environment:

```bash
source env/bin/activate
```

On Windows PowerShell, activate with `.\env\Scripts\Activate.ps1` instead.

## Calibrate the camera

Calibration uses photos of a ChArUco board to estimate camera intrinsics and lens
distortion. The configured board is **5 × 7 squares**, `DICT_6X6_250`, with
**34 mm squares** and **20.4 mm markers**. The printed marker IDs and layout must
match. Mount it flat on a rigid surface and measure the squares; do not stretch
the print. To generate a matching board if needed:

```bash
python -m vision.utils.calibration generate
```

1. Capture **20–30 sharp views** of the board:

   ```bash
   python -m vision.utils.capture
   ```

   Click the preview, then press **c / space** to save an original PNG to
   `media/capture_*.png`. Move the board across the image, vary distance, and tilt
   it around both axes. Hold still for each shot. **q / Escape** quits.
   `--require-board` rejects shots without enough usable corners.

2. Solve and save the calibration:

   ```bash
   python -m vision.utils.calibration calibrate
   ```

   Requires at least **10 usable views** at one resolution. Saves camera
   parameters and all per-image errors to `calibration/camera_calibration.npz`,
   replacing an existing result. Review RMS error and flagged images; inspect
   poor shots, move them out of the dataset, and recapture as needed. Low RMS
   alone does not guarantee accuracy.

3. Verify straight edges in the raw (left) and corrected (right) previews:

   ```bash
   python -m vision.utils.calibration verify
   ```

Defaults request **camera 0, 1920 × 1080, 60 FPS**. Use `--camera 1` if needed;
the driver may negotiate a different mode. Keep the **same camera, resolution,
camera mode, and focus** for capture, verification, and tracking. Calibration
rejects mismatched tracking resolutions. On macOS, use the UVC focus helper below
to disable autofocus if the camera exposes the control. Lock focus before
calibrating and keep it fixed for tracking.

### Keep autofocus disabled

Autofocus may remain disabled while the webcam stays powered, but the setting
is not guaranteed to persist after unplugging the camera, restarting the Mac,
cycling USB hub power, or another application changing camera settings.
`vision.run` does not automatically reapply the setting.

Before each tracking session, run this from `lasertrackinggimbal-ece445`:

```bash
env/bin/python -m vision.camera_focus \
  --device "FHD Camera Microphone" --disable-auto
```

`FHD Camera Microphone` is the connected NBPower webcam's USB product name.
The helper checks support and verifies the setting by reading it back. Use
`env/bin/python -m vision.camera_focus --list` to find the name if it changes.
The helper requires `uvc-util` at `Tools/bin/uvc-util` or on `PATH`.
Disabling autofocus does not restore a previously calibrated manual lens
position; keep the same focus used for calibration and check image sharpness
after reconnecting the camera.

For different measured squares, pass `--square-length METERS` to both capture and
calibration; markers default to 0.6 times that length. Use `--marker-length` if
the ratio differs. Keep separate capture folders for different setups with
`python -m vision.utils.capture --output vision/media/session1` and
`python -m vision.utils.calibration calibrate --images vision/media/session1`.

## Run capture, detection, and positioning

```bash
python -m vision.run
python -m vision.run --ethernet
```

This opens the live camera, detects the ball with
`weights/coreml/yolo26s-384x640-fp16.mlpackage` (Core ML FP16 on macOS), and uses
the saved calibration to estimate position and pan/tilt errors. **q / Escape**
quits. It processes live frames; it does not save images or video.

The preview shows confidence, approximate X/Y/Z in meters, range, angles, and
processing FPS. Coordinates are **+X right, +Y down, +Z forward**; positive pan
is right and positive tilt is up. Range is distance to the ball center. Depth
uses the ball diameter (**0.24 m** by default); set `--ball-diameter METERS` to
your measured value. Boxes touching the image edge have no depth estimate.

Optional: `--camera 1` selects another camera. To use the original PyTorch model
on the Apple GPU, pass `--model vision/weights/yolo26s.pt --device mps`.
On Windows with the RTX 4060, use
`python -m vision.run --model vision/weights/yolo26s.pt --device 0` with a
CUDA-enabled PyTorch installation (see the [PyTorch installer](https://pytorch.org/get-started/locally/)).
The actual inference device is printed at startup.
`--log-csv vision/measurements/run1.csv` saves per-frame measurements without
overwriting an existing file. Capture remains 1920 × 1080; model input defaults
to 640 pixels. Keep `--imgsz`, `--model`, and `--confidence` at their defaults
unless deliberately comparing accuracy and processing speed. Use `--help` for
all available options.

Tracking now uses a latest-frame capture worker for numeric live camera indices
(`--sync-capture` restores synchronous capture), warms up inference before opening
the camera, and limits preview refresh to 30 FPS at a maximum 1280 × 720.
`--no-preview` removes GUI overhead; `--max-frames 600` makes a bounded measurement run.
Core ML `.mlpackage` models use the direct CPU + Neural Engine path; `--device`
selects hardware only for PyTorch models. Camera delivery FPS, discarded frames,
and host receipt age are included in runtime diagnostics.

Run the automated vision tests with `python -m unittest discover -s vision/unit_tests -t . -v`.
See [unit_tests/README.md](unit_tests/README.md) for scope and individual test commands.
