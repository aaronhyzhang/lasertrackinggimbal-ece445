import argparse
from pathlib import Path
import sys
import time

import cv2 as cv

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = "vision"

from .detect import DEFAULT_IMAGE_SIZE, Detect
from .position import BALL_DIAMETER_M, Position
from .stream import Stream
from .utils.draw import draw_result, draw_text
from .utils.logging import Measurement, MeasurementLogger


DIRECTORY = Path(__file__).resolve().parent
MODEL = "weights/yolo26s.pt"
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
    parser.add_argument("--model", default=str(DIRECTORY / MODEL))
    parser.add_argument("--calibration", default=str(DIRECTORY / CALIBRATION))
    parser.add_argument("--ball-diameter", type=float, default=BALL_DIAMETER_M,
                        help="Actual ball diameter in meters")
    parser.add_argument("--confidence", type=float, default=CONFIDENCE)
    parser.add_argument("--imgsz", type=int, default=DEFAULT_IMAGE_SIZE)
    parser.add_argument("--device", default=None,
                        help="cpu, mps (Mac), or 0 / cuda:0 (NVIDIA GPU); default: automatic")
    parser.add_argument("--image", help="Test one local image instead of the live camera")
    parser.add_argument("--log-csv", help="Save every processed frame's measurements to a new CSV file")
    args = parser.parse_args(argv)
    stream = None
    logger = None
    try:
        position = Position(args.calibration, ball_diameter_m=args.ball_diameter)
        detector = Detect(args.model, confidence=args.confidence, image_size=args.imgsz,
                          device=args.device)
        if args.log_csv:
            logger = MeasurementLogger(args.log_csv)
        if args.image:
            frame = cv.imread(str(Path(args.image).expanduser()))
            if frame is None:
                raise ValueError(f"Cannot read image: {args.image}")
        else:
            stream = Stream(args.camera, width=args.width, height=args.height, fps=args.fps)
        cv.namedWindow("Ball tracking", cv.WINDOW_NORMAL)
        cv.resizeWindow("Ball tracking", 1280, 720)
        print("q/Escape: quit | Camera frame: +pan right, +tilt up, +Z forward.")
        print(f"Ball diameter: {args.ball_diameter:.4f} m | Model input target: {args.imgsz} px | "
              f"Device requested: {args.device or 'auto'}")
        previous_loop_ms = 0.0
        previous_display_ms = 0.0
        measured_fps = 0.0
        session_start = time.perf_counter()
        frame_index = 0
        while True:
            loop_start = time.perf_counter()
            if stream is not None:
                frame = stream.read()
            # Receipt time is not the sensor exposure time or end-to-end latency.
            frame_received = time.perf_counter()
            elapsed_s = frame_received - session_start
            capture_ms = (frame_received - loop_start) * 1000 if stream is not None else 0.0
            frame_index += 1
            image_size = (frame.shape[1], frame.shape[0])
            position.calibration.validate_image_size(image_size)
            bbox = detector.detect(frame)
            if frame_index == 1:
                print(f"Actual inference device: {detector.actual_device}")
                if args.device == "mps" and not (detector.actual_device or "").startswith("mps"):
                    print("WARNING: MPS was requested but is not active; check PyTorch MPS availability.")
            result = None
            geometry_error = None
            if bbox.size:
                try:
                    result = position.estimate(bbox, image_size)
                except ValueError as error:
                    geometry_error = str(error)
            display_start = time.perf_counter()
            lines = draw_result(frame, bbox, result)
            if geometry_error:
                lines.append(f"Geometry unavailable: {geometry_error}")
            lines.append(f"Read: {capture_ms:.1f} ms | Detect: {detector.inference_ms:.1f} ms | "
                         f"Previous draw/display: {previous_display_ms:.1f} ms")
            lines.append(f"Previous loop: {previous_loop_ms:.1f} ms | "
                         f"Measured FPS: {measured_fps:.1f}")
            if args.image:
                lines[-1] = f"Inference: {detector.inference_ms:.1f} ms | Static image"
            draw_text(frame, lines)
            cv.imshow("Ball tracking", frame)
            key = cv.waitKey(1) & 0xFF
            display_ms = (time.perf_counter() - display_start) * 1000
            previous_display_ms = display_ms
            loop_ms = (time.perf_counter() - loop_start) * 1000
            previous_loop_ms = loop_ms
            instantaneous_fps = 1000 / loop_ms if loop_ms > 0 else 0.0
            measured_fps = instantaneous_fps if measured_fps == 0 else 0.9 * measured_fps + 0.1 * instantaneous_fps
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
                while key not in (ord("q"), 27) and cv.getWindowProperty("Ball tracking", cv.WND_PROP_VISIBLE) >= 1:
                    key = cv.waitKey(50) & 0xFF
                break
            if key in (ord("q"), 27) or cv.getWindowProperty("Ball tracking", cv.WND_PROP_VISIBLE) < 1:
                break
    except (OSError, ValueError, RuntimeError, cv.error) as error:
        parser.exit(1, f"Error: {error}\n")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if logger is not None:
            logger.close()
        if stream is not None:
            stream.release()
        cv.destroyAllWindows()


if __name__ == "__main__":
    main()
