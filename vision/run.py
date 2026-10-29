import argparse
from collections import deque
import math
from pathlib import Path
import sys
import time

import cv2 as cv

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = "vision"

from .detect import DEFAULT_IMAGE_SIZE, Detect
from .gimbal import CONFIG_PATH, Gimbal, Settings
from .live_telemetry import TelemetryRecorder
from .position import BALL_DIAMETER_M, Position
from .stream import Stream
from .utils.diagnostics import Diagnostics
from .utils.draw import draw_result, draw_text
from .utils.logging import Measurement, MeasurementLogger


DIRECTORY = Path(__file__).resolve().parent
MODEL = "weights/coreml/yolo26s-384x640-fp16.mlpackage"
CAMERA = "0"
CALIBRATION = "calibration/camera_calibration.npz"
WIDTH = 1920
HEIGHT = 1080
FPS = 60
CONFIDENCE = 0.15


def main(argv=None):
    parser = argparse.ArgumentParser(description="Pipeline for streaming, detection, and position estimation.")
    parser.add_argument("--camera", default=CAMERA)
    parser.add_argument("--width", type=int, default=WIDTH)
    parser.add_argument("--height", type=int, default=HEIGHT)
    parser.add_argument("--fps", type=float, default=FPS)
    parser.add_argument("--model", default=str(DIRECTORY / MODEL),
                        help="Model path; default: YOLO26s Core ML FP16, 384x640 (macOS)")
    parser.add_argument("--calibration", default=str(DIRECTORY / CALIBRATION))
    parser.add_argument("--ball-diameter", type=float, default=BALL_DIAMETER_M,
                        help="Actual ball diameter in meters")
    parser.add_argument("--confidence", type=float, default=CONFIDENCE)
    parser.add_argument("--imgsz", type=int, default=DEFAULT_IMAGE_SIZE)
    parser.add_argument("--device", default="mps",
                        help="cpu, mps (default), or 0 / cuda:0; Core ML uses --coreml-compute-units")
    parser.add_argument("--half", action="store_true", help="Try FP16 for PyTorch inference")
    parser.add_argument("--coreml-compute-units", choices=("cpu-and-ne", "cpu-only"), default="cpu-and-ne")
    parser.add_argument("--sync-capture", action="store_true", help="Disable the live latest-frame capture worker")
    parser.add_argument("--no-preview", action="store_true", help="Skip GUI work; Ctrl-C stops tracking")
    parser.add_argument("--preview-fps", type=float, default=30, help="Preview refresh limit; 0 draws every frame")
    parser.add_argument("--max-frames", type=int, default=0, help="Stop after this many processed frames; 0 is unlimited")
    parser.add_argument("--image", help="Test one local image instead of the live camera")
    parser.add_argument("--log-csv", help="Save every processed frame's measurements to a new CSV file")
    parser.add_argument("--debug-log", help="Runtime log path (default: vision/logs/run-<timestamp>.log)")
    parser.add_argument("--no-telemetry", action="store_true", help="Disable automatic DIAG/HEALTH recording during Ethernet runs")
    ethernet = parser.add_mutually_exclusive_group()
    ethernet.add_argument("--ethernet", action="store_true",
                          help="Track through the PCB; press physical S1 once when prompted")
    ethernet.add_argument("--ethernet-dry-run", action="store_true",
                          help="Read PCB feedback and preview targets without motion commands")
    parser.add_argument("--gimbal-config", type=Path, default=CONFIG_PATH)
    args = parser.parse_args(argv)
    if args.preview_fps < 0 or not math.isfinite(args.preview_fps) or args.max_frames < 0:
        parser.error("Preview FPS and max frames must be finite and nonnegative")
    if args.image and (args.ethernet or args.ethernet_dry_run):
        parser.error("Ethernet tracking requires a live camera, not --image")
    if (args.ethernet or args.ethernet_dry_run) and not str(args.camera).strip().isdecimal():
        parser.error("Ethernet tracking requires a numeric live camera index (e.g. --camera 0); video replay is disabled")
    stream = None
    logger = None
    gimbal = None
    telemetry = None
    diagnostics = Diagnostics(args.debug_log)
    print(f"Runtime diagnostics: {diagnostics.path}")
    diagnostics.log.info("Arguments: %s", vars(args))
    try:
        diagnostics.mark("calibration")
        position = Position(args.calibration, ball_diameter_m=args.ball_diameter)
        diagnostics.mark("model_load")
        detector = Detect(args.model, confidence=args.confidence, image_size=args.imgsz,
                          device=args.device, half=args.half, coreml_compute_units=args.coreml_compute_units)
        if not args.image:
            diagnostics.mark("model_warmup")
            detector.warmup(args.width, args.height)
        if args.log_csv:
            logger = MeasurementLogger(args.log_csv)
        diagnostics.mark("capture_setup")
        if args.image:
            frame = cv.imread(str(Path(args.image).expanduser()))
            if frame is None:
                raise ValueError(f"Cannot read image: {args.image}")
        else:
            stream = Stream(args.camera, width=args.width, height=args.height, fps=args.fps,
                            latest=not args.sync_capture and str(args.camera).strip().isdecimal())
            diagnostics.log.info("Camera negotiated: %s", stream.mode)
        if not args.no_preview:
            cv.namedWindow("Ball tracking", cv.WINDOW_NORMAL)
            cv.resizeWindow("Ball tracking", 1280, 720)
        if args.ethernet or args.ethernet_dry_run:
            settings = Settings.load(args.gimbal_config)
            diagnostics.log.info("Gimbal settings: %s", settings)
            diagnostics.mark("gimbal_start")
            gimbal = Gimbal(settings, dry_run=args.ethernet_dry_run)
            gimbal.start()
            if not args.no_telemetry:
                telemetry = TelemetryRecorder(settings, diagnostics.path, diagnostics.log,
                                              host_snapshot=gimbal.telemetry_snapshot)
                telemetry.start()
                print(f"Telemetry CSV: {telemetry.csv_path}")
            geometry = "coupled yaw/tilt" if settings.tilt_level_deg is not None else "local angle corrections (near-level camera)"
            print(f"Ethernet: {settings.host}, tilt:{settings.tilt_port}, pan:{settings.pan_port} | {geometry}")
            print("Encoder feedback + camera errors set targets. q/Escape/S2 stops; no automatic rearm.")
        print("q/Escape: quit | Camera frame: +pan right, +tilt up, +Z forward.")
        print(f"Ball diameter: {args.ball_diameter:.4f} m | Model input target: {args.imgsz} px | "
              f"Execution backend: {detector.actual_device or args.device or 'auto'}")
        previous_loop_ms = 0.0
        previous_display_ms = 0.0
        measured_fps = 0.0
        session_start = time.perf_counter()
        frame_index = 0
        last_preview = float("-inf")
        frame_times = deque(maxlen=120)
        while True:
            diagnostics.mark("gimbal_check", frame=frame_index + 1)
            loop_start = time.perf_counter()
            if gimbal is not None:
                gimbal.check()
            if stream is not None:
                diagnostics.mark("capture")
                frame = stream.read()
            # Receipt time is not the sensor exposure time or end-to-end latency.
            capture_end = time.perf_counter()
            frame_received = stream.received_at if stream is not None else capture_end
            elapsed_s = frame_received - session_start
            capture_ms = (capture_end - loop_start) * 1000 if stream is not None else 0.0
            diagnostics.mark("frame_received", frame_age_ms=(capture_end - frame_received) * 1000,
                             capture_ms=capture_ms)
            if stream is not None:
                diagnostics.mark("frame_received", camera_fps=stream.capture_fps,
                                 dropped_frames=stream.dropped_frames)
            frame_index += 1
            image_size = (frame.shape[1], frame.shape[0])
            position.calibration.validate_image_size(image_size)
            diagnostics.mark("inference")
            bbox = detector.detect(frame)
            diagnostics.mark("position", device=detector.actual_device, inference_ms=detector.inference_ms)
            if frame_index == 1:
                print(f"Actual inference device: {detector.actual_device}")
                if args.device == "mps" and not (detector.actual_device or "").startswith(("mps", "coreml:")):
                    print("WARNING: MPS was requested but is not active; check PyTorch MPS availability.")
            result = None
            geometry_error = None
            if bbox.size:
                try:
                    result = position.estimate(bbox, image_size)
                except ValueError as error:
                    geometry_error = str(error)
            if gimbal is not None:
                diagnostics.mark("gimbal_update")
                gimbal.update(result, frame_received)
            diagnostics.mark("display")
            display_start = time.perf_counter()
            lines = []
            if gimbal is not None:
                gimbal_status = gimbal.check()
                lines.append(gimbal_status)
                diagnostics.mark("display", gimbal_status=gimbal_status)
            if geometry_error:
                lines.append(f"Geometry unavailable: {geometry_error}")
            lines.append(f"Read: {capture_ms:.1f} ms | Detect: {detector.inference_ms:.1f} ms | "
                         f"Previous draw/display: {previous_display_ms:.1f} ms")
            lines.append(f"Previous loop: {previous_loop_ms:.1f} ms | "
                         f"Measured FPS: {measured_fps:.1f}")
            if args.image:
                lines[-1] = f"Inference: {detector.inference_ms:.1f} ms | Static image"
            if not args.no_preview and (args.preview_fps == 0 or display_start - last_preview >= 1 / args.preview_fps):
                # Draw on a private frame; inference/capture pixels remain untouched.
                preview = frame.copy()
                lines = draw_result(preview, bbox, result) + lines
                scale = min(1.0, 1280 / preview.shape[1], 720 / preview.shape[0])
                if scale < 1:
                    preview = cv.resize(preview, None, fx=scale, fy=scale, interpolation=cv.INTER_LINEAR)
                draw_text(preview, lines)
                cv.imshow("Ball tracking", preview)
                last_preview = display_start
            key = -1 if args.no_preview else cv.waitKey(1) & 0xFF
            display_ms = (time.perf_counter() - display_start) * 1000
            previous_display_ms = display_ms
            loop_ms = (time.perf_counter() - loop_start) * 1000
            previous_loop_ms = loop_ms
            frame_times.append(time.perf_counter())
            # Throughput over elapsed time, including the previous iteration's logging.
            # Averaging reciprocal frame times exaggerates FPS when frames are uneven.
            measured_fps = ((len(frame_times) - 1) / max(frame_times[-1] - frame_times[0], 1e-9)
                            if len(frame_times) > 1 else 1000 / max(loop_ms, 1e-9))
            diagnostics.mark("measurement_log", loop_ms=loop_ms, fps=measured_fps, display_ms=display_ms)
            if logger is not None:
                logger.log(Measurement(
                    frame_index=frame_index, elapsed_s=elapsed_s, bbox=bbox, result=result,
                    geometry_error=geometry_error, inference_ms=detector.inference_ms,
                    loop_ms=loop_ms, fps=measured_fps, ball_diameter_m=args.ball_diameter,
                    imgsz=args.imgsz, capture_ms=capture_ms, display_ms=display_ms,
                    device_requested=args.device, device_actual=detector.actual_device,
                ), flush=bool(args.image))
            if args.image:
                # Keep the image visible without rerunning inference or modifying it repeatedly.
                diagnostics.mark("image_wait")
                while not args.no_preview and key not in (ord("q"), 27) and cv.getWindowProperty("Ball tracking", cv.WND_PROP_VISIBLE) >= 1:
                    key = cv.waitKey(50) & 0xFF
                break
            if args.max_frames and frame_index >= args.max_frames:
                break
            if not args.no_preview and (key in (ord("q"), 27) or cv.getWindowProperty("Ball tracking", cv.WND_PROP_VISIBLE) < 1):
                break
    except (OSError, ValueError, RuntimeError, cv.error) as error:
        diagnostics.log.exception("Tracking failed; last state=%s", diagnostics.state)
        parser.exit(1, f"Error: {error}\nDiagnostics: {diagnostics.path}\n")
    except KeyboardInterrupt:
        diagnostics.log.info("Keyboard interrupt")
        print("\nStopped.")
    except Exception:
        diagnostics.log.exception("Unexpected failure; last state=%s", diagnostics.state)
        raise
    finally:
        diagnostics.mark("cleanup")
        for name, cleanup in (("gimbal", gimbal.close if gimbal is not None else None),
                              ("telemetry", telemetry.close if telemetry is not None else None),
                              ("measurements", logger.close if logger is not None else None),
                              ("camera", stream.release if stream is not None else None),
                              ("windows", cv.destroyAllWindows)):
            if cleanup is not None:
                try:
                    cleanup()
                except Exception:
                    diagnostics.log.exception("Cleanup failed: %s", name)
        diagnostics.close()


if __name__ == "__main__":
    main()
