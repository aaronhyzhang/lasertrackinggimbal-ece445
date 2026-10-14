# Basketball detector: resolution, model size, and frame rate

Start by comparing **YOLO26s at 640** with **YOLO26n at 960**. They have similar
CPU cost here and test different hypotheses: more model capacity versus more
image detail. Keep n640 as the default baseline until a recorded moving-ball
comparison establishes the better combination of recall, box fit, and delay.

## Measurements from this environment

CPU PyTorch inference on a saved native 1920 × 1080 frame
(`media/capture_0001.png`), one frame per call, three warmups followed by seven
measurements per configuration. Medians below include prediction preprocessing,
model execution, and postprocessing, but exclude camera capture and GUI work.
No other model benchmark ran concurrently.

| Model / `--imgsz` | Predict time | Prediction-only rate ceiling | Potential benefit and cost |
| --- | ---: | ---: | --- |
| n / 640 | 28.7 ms | 34.9 FPS | Current baseline, fastest tested option. |
| n / 960 | 56.0 ms | 17.9 FPS | Retains more detail; roughly twice the CPU time. |
| n / 1280 | 99.4 ms | 10.1 FPS | More detail, but substantially fewer updates. |
| n / 1920 | 210.5 ms | 4.8 FPS | Retains full capture detail, with a high CPU cost. |
| s / 640 | 49.3 ms | 20.3 FPS | Greater model capacity at the same image detail. |
| m / 640 | 115.4 ms | 8.7 FPS | More capacity again; requires evidence to justify the delay. |

Actual application FPS will be lower than these reciprocal-time ceilings.
Environment: macOS arm64, PyTorch 2.14.0, Ultralytics 8.4.154, OpenCV 5.0.0,
actual device CPU, eight PyTorch intra-operation threads during prediction.
Full measurements, including min/max and tensor shapes, are in
[benchmarks/cpu_results.json](benchmarks/cpu_results.json).

The 1080p image is a calibration-board frame with no ball, so it measures runtime,
not ball accuracy. The same configurations were also tested on `media/test2.jpg`
(992 × 558); all detected a ball, with similar timings. That one static,
unannotated image does not establish moving-ball recall or accurate boxes.
Higher-resolution inference upsamples that smaller image beyond its native size.

An earlier check on `media/test1.webp` found that increasing input size changed
the highest-scoring selection from the large foreground ball to a small
background ball. Both are real balls. More resolution can therefore alter target
selection; it is not a guarantee that the intended ball is followed.

## What the two upgrades change

Increasing input resolution retains spatial information that resizing discards.
A 200-pixel ball in a 1920-wide camera frame becomes roughly 67 pixels wide at
640, 100 at 960, 133 at 1280, and 200 at 1920. This can help distant balls and box
edges. It does not add model capacity or repair blur, occlusion, or unfamiliar
appearances. Your example ball is not extremely tiny even at 640, so resolution
alone is not a demonstrated explanation for its intermittent misses.

For this 16:9 input with rectangular padding, actual model tensors were
384 × 640, 544 × 960, 736 × 1280, and 1088 × 1920 (height × width). Thus 1920
has about 8.5 times the input pixels of 640, while preserving the original image
detail and adding eight vertical padding pixels. Returned boxes are scaled
back to the original camera coordinates; changing `imgsz` does not change the
calibration. [Ultralytics prediction arguments](https://docs.ultralytics.com/modes/predict/)
describe input resizing and device selection.

Increasing model size gives the detector more capacity at the same input detail.
Official COCO mAP50–95 at 640 is 40.9 for n, 48.6 for s, and 53.1 for m; the
corresponding end-to-end-head values are 40.1, 47.8, and 52.5. These are general
dataset metrics, not basketball-specific recall, depth accuracy, or local speed.
The [official YOLO26 table](https://docs.ultralytics.com/models/yolo26/) also lists
CPU ONNX timings on different hardware; use the local measurements above for
this PyTorch comparison. A larger model cannot recover detail removed from its
input, and a more confident box is not necessarily a more accurate box.

## Why the app shows about 16 FPS when the request is 60

The camera's requested mode, driver-reported FPS, and processed-frame rate are
different quantities. The original loop runs sequentially:

`read/decode → detector → geometry → draw → display/GUI polling`

Sixty processed frames per second requires the entire loop to finish in
16.7 ms. Your detector alone takes 27–29 ms, limiting prediction to about
34–37 FPS even with zero other work. Your measured loops take 51–65 ms,
consistent with roughly 15–20 FPS and the displayed smoothed value of 16.
The remaining time includes camera read/wait/decode and display work; the old
log does not identify how much each contributes.

The updated preview and CSV show `capture_ms`, `inference_ms`, and `display_ms`.
The detector timer includes preprocessing and result extraction, not only neural
network execution. `loop_ms` also includes geometry and excludes subsequent
CSV/console logging, so use frame-receipt timestamp differences for overall
cadence when logging. Neither metric proves exposure-to-display latency.

The stream requests a one-frame camera buffer, but the backend can reject it.
If images are queued while processing is slower than capture, the boxes may be
correct on old frames yet lag the real ball. A latest-frame capture worker can
reduce stale frames by discarding older ones; it cannot make a 29 ms detector
produce 60 independent detections per second. Measure delay before adding it.

Batch size one does not make inference cheap: convolution operations within a
single frame are highly parallel. The Apple GPU is worth a controlled trial.
PyTorch's [MPS backend](https://docs.pytorch.org/docs/2.14/notes/mps.html) executes
supported operations on that GPU. This execution environment reports MPS built
but unavailable, so no GPU timing is claimed. Run the commands below locally,
then check **Actual inference device**. The installed library can silently fall
back to CPU; the app now reports and warns about that case.

## Commands to compare

From `vision/`, with the existing environment active, use unique CSV names and
the same camera, calibrated focus, illumination, and ball path:

```bash
python run.py --model weights/yolo26n.pt --imgsz 640 --device cpu --log-csv measurements/n640_cpu.csv
python run.py --model weights/yolo26n.pt --imgsz 960 --device cpu --log-csv measurements/n960_cpu.csv
python run.py --model weights/yolo26s.pt --imgsz 640 --device cpu --log-csv measurements/s640_cpu.csv
python run.py --model weights/yolo26m.pt --imgsz 640 --device cpu --log-csv measurements/m640_cpu.csv
python run.py --model weights/yolo26s.pt --imgsz 640 --device mps --log-csv measurements/s640_mps.csv
```

The s/m checkpoints are now available in `weights/`, downloaded from the official
Ultralytics assets release:
[yolo26s.pt](https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26s.pt),
[yolo26m.pt](https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26m.pt).
The app still refuses missing model paths rather than downloading on launch.

Use repeated live trials to measure responsiveness; use the same recorded frames
and manual annotations to compare accuracy fairly. Keep confidence at 0.25
initially. Score correct-ball recall, box center/diameter error, distance error,
and longest missed interval as described in [VALIDATION.md](VALIDATION.md).
The CSV records measurements but does not record video.

## Other causes to isolate

- **Per-frame confidence cutoff:** scores below 0.25 disappear. A separate 0.15
  trial can reveal marginal detections, but it can introduce false selections.
- **No target identity:** every frame picks the most confident generic sports
  ball. Distractors can take over; there is no temporal association or prediction.
- **Training mismatch:** background, lighting, basketball texture, hands, motion,
  and partial occlusion may differ from the model's training examples. If sharp
  frames still fail, a labeled local dataset and basketball fine-tuning may help
  more than sending every frame at 1920. Validate on separate recordings.
- **Focus changes:** temporarily reduce detail and can change intrinsics. Use
  the calibrated focus when possible and confirm that the backend honors it.
- **Motion blur or shutter distortion:** inspect original failure frames, even
  if the preview looks recognizable to a person. Address lighting/exposure for
  blur; do not assume this camera has a particular sensor readout type.
- **Loose boxes:** ordinary detection optimizes object localization, not physical
  diameter measurement. A ball silhouette/contour refinement or segmentation may
  improve depth after detection; compare against manually marked boxes first.

Tracking can bridge short misses and preserve identity, but predicted positions
must be marked separately from current measurements. Avoid judging success only
by how smooth the overlay looks.
