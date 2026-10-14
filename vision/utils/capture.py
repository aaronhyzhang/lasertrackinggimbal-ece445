"""Preview a camera and save one untouched, full-resolution image per keypress."""

import argparse
from pathlib import Path
import re
import time

import cv2 as cv

from .calibration import add_board_arguments, calibration_from_args
from ..stream import Stream


CAMERA = "0"
WIDTH = 1920
HEIGHT = 1080
FPS = 60
MEDIA = Path(__file__).resolve().parent.parent / "media"
MIN_CORNERS = 6


def next_capture_number(directory: Path) -> int:
    numbers = []
    for path in directory.glob("capture_*.png"):
        match = re.fullmatch(r"capture_(\d+)\.png", path.name)
        if match:
            numbers.append(int(match.group(1)))
    return max(numbers, default=0) + 1


def save_capture(frame, directory: Path, number: int) -> tuple[Path, int]:
    """Use exclusive creation so an existing image is never overwritten."""
    ok, encoded = cv.imencode(".png", frame)
    if not ok:
        raise RuntimeError("OpenCV could not encode the captured image.")
    while True:
        path = directory / f"capture_{number:04d}.png"
        try:
            with path.open("xb") as output:
                output.write(encoded.tobytes())
            return path, number + 1
        except FileExistsError:
            number += 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", default=CAMERA, help="Webcam index or video/device path.")
    parser.add_argument("--width", type=int, default=WIDTH)
    parser.add_argument("--height", type=int, default=HEIGHT)
    parser.add_argument("--fps", type=float, default=FPS)
    parser.add_argument("--output", type=Path, default=MEDIA, help="Image directory (default: vision/media).")
    parser.add_argument("--require-board", action="store_true",
                        help="Only save if at least six non-collinear ChArUco corners are detected.")
    add_board_arguments(parser)
    args = parser.parse_args(argv)
    stream = None
    saved = valid_saved = 0
    try:
        calibration = calibration_from_args(args)
        args.output.mkdir(parents=True, exist_ok=True)
        number = next_capture_number(args.output)
        stream = Stream(args.camera, width=args.width, height=args.height, fps=args.fps)
        print(f"Saving original PNGs to {args.output.resolve()}")
        print("Click the preview, then c/space: save one image; q/Escape: quit.")
        window = "NBPOWER capture - c: save | q: quit"
        cv.namedWindow(window, cv.WINDOW_NORMAL)
        cv.resizeWindow(window, 1280, 720)
        previous = time.perf_counter()
        measured_fps = 0.0
        message = "Ready - c/space saves the original frame"
        while True:
            frame = stream.read()
            corners, ids = calibration.detect(frame)
            count = 0 if ids is None else len(ids)
            usable = calibration.has_usable_corners(corners, ids, MIN_CORNERS)
            preview = frame.copy()
            if count:
                # OpenCV 5 detection can return Nx2; drawing still expects Nx1x2.
                cv.aruco.drawDetectedCornersCharuco(preview, corners.reshape(-1, 1, 2), ids)
            # Resize the preview only. Detection and saved files retain native pixels.
            scale = min(1.0, 1280 / frame.shape[1], 720 / frame.shape[0])
            if scale < 1:
                preview = cv.resize(preview, None, fx=scale, fy=scale, interpolation=cv.INTER_AREA)
            now = time.perf_counter()
            instant_fps = 1 / max(now - previous, 1e-9)
            measured_fps = instant_fps if not measured_fps else 0.9 * measured_fps + 0.1 * instant_fps
            previous = now
            color = (0, 220, 0) if usable else (0, 165, 255)
            lines = [
                f"{frame.shape[1]}x{frame.shape[0]} | Preview {measured_fps:.1f} FPS | Corners {count}",
                f"Saved {saved} ({valid_saved} usable) | c/space: save | q/Esc: quit",
                message,
            ]
            for row, line in enumerate(lines):
                cv.putText(preview, line, (12, 28 + row * 28), cv.FONT_HERSHEY_SIMPLEX,
                           0.6, (0, 0, 0), 4, cv.LINE_AA)
                cv.putText(preview, line, (12, 28 + row * 28), cv.FONT_HERSHEY_SIMPLEX,
                           0.6, color, 1, cv.LINE_AA)
            cv.imshow(window, preview)
            key = cv.waitKey(1) & 0xFF
            if key in (ord("q"), 27) or cv.getWindowProperty(window, cv.WND_PROP_VISIBLE) < 1:
                break
            if key in (ord("c"), ord(" ")):
                if args.require_board and not usable:
                    message = "Not saved: need 6+ non-collinear ChArUco corners"
                    print(message)
                    continue
                path, number = save_capture(frame, args.output, number)
                saved += 1
                valid_saved += int(usable)
                message = f"Saved {path.name} ({count} corners)"
                print(f"{message}: {path}")
                if not usable:
                    print("  Board not usable with current settings; calibration may reject this image.")
        return 0
    except (OSError, ValueError, RuntimeError, cv.error) as error:
        print(f"Capture error: {error}")
        return 1
    except KeyboardInterrupt:
        return 0
    finally:
        if stream is not None:
            stream.release()
        cv.destroyAllWindows()
        print(f"Session complete: {saved} images saved, {valid_saved} with usable board corners.")


if __name__ == "__main__":
    raise SystemExit(main())
