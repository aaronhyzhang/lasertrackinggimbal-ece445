# Vision tests

From the project root, with the vision environment active:

```bash
python -m unittest discover -s vision/unit_tests -t . -v
```

Run one module, for example:

```bash
python -m unittest vision.unit_tests.test_position -v
python -m unittest vision.unit_tests.test_image_integration -v
```

Uses Python's built-in `unittest` and the existing vision dependencies; no extra
test packages are required. Runs on CPU on macOS or Windows without a webcam,
GPU, or interactive windows. All generated files go to temporary directories.

| Module | Checks |
| --- | --- |
| `test_detect.py` | Model configuration, ball selection, confidence filtering, input validation, backend reporting |
| `test_position.py` | Known depths/angles, diameter scaling, clipping, invalid boxes and calibration resolution |
| `test_stream.py` | Camera mode requests, original frames, Windows paths, read/open failures, cleanup |
| `test_timing.py` | Decorator timing, unchanged returns/metadata, per-instance measurements, exception propagation |
| `test_draw.py` | No-ball, confidence-only, position/clipped overlays, text rendering |
| `test_logging.py` | CSV schema/values, missing detections, errors, flushing, overwrite protection, close |
| `test_capture.py` | Filename numbering, lossless PNGs, collision protection, board filtering, capture loop |
| `test_calibration.py` | Board generation/detection, corner checks, loading/validation, solve, verification, CLI dispatch |
| `test_run.py` | Imports/entry points, multi-frame loop, device forwarding, CSV, errors and cleanup |
| `test_image_integration.py` | Real local YOLO inference on `vision/media/test2.jpg`, positioning/drawing, complete image-to-CSV pipeline |

The integration tests require the existing `vision/media/test2.jpg` and
`vision/weights/yolo26s.pt`. They do not download weights. The real saved camera
calibration is used only to check rejection of the unrelated image resolution.
Geometry tests use explicitly synthetic intrinsics matching the fixture's
992 × 558 resolution; these do **not** validate physical distance accuracy.
Calibration solving is checked against known projected board views.

Camera/GUI boundaries are mocked. NVIDIA/MPS selectors are tested for correct
forwarding, but actual camera operation and GPU performance need hardware tests.
