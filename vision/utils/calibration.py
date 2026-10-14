"""Generate, solve, load, and inspect a ChArUco camera calibration."""

import argparse
from pathlib import Path
import struct
import sys
import zipfile
import zlib

import cv2 as cv
import numpy as np


DIRECTORY = Path(__file__).resolve().parent.parent
DEFAULT_CALIBRATION = DIRECTORY / "calibration" / "camera_calibration.npz"
DEFAULT_BOARD = DIRECTORY / "calibration" / "charuco_board.png"
DEFAULT_IMAGES = DIRECTORY / "media"
SQUARES_X = 5
SQUARES_Y = 7
SQUARE_LENGTH = 0.034  # Meters; measured printed squares are 3.4 cm x 3.4 cm.
MARKER_RATIO = 0.6  # Matches the OpenCV charucoboard.png already printed by the user.
DICTIONARY = "DICT_6X6_250"
MIN_CORNERS = 6
MIN_VIEWS = 10


class Calibration:
    def __init__(self, squares_x: int = SQUARES_X, squares_y: int = SQUARES_Y,
                 square_length: float = SQUARE_LENGTH,
                 marker_length=None,
                 dictionary: str = DICTIONARY, legacy_pattern: bool = False):
        if (not isinstance(squares_x, (int, np.integer)) or
                not isinstance(squares_y, (int, np.integer)) or
                squares_x < 2 or squares_y < 2):
            raise ValueError("The board needs at least two squares along each axis.")
        if marker_length is None:
            marker_length = square_length * MARKER_RATIO
        if (not np.isfinite(square_length) or not np.isfinite(marker_length) or
                not 0 < marker_length < square_length):
            raise ValueError("Board lengths must satisfy 0 < marker_length < square_length (meters).")
        if not hasattr(cv, "aruco") or not hasattr(cv.aruco, "CharucoDetector"):
            raise RuntimeError("Install a current opencv-contrib-python with CharucoDetector support.")
        if not isinstance(dictionary, str) or not dictionary.startswith("DICT_"):
            raise ValueError("Use an OpenCV dictionary name, e.g. DICT_6X6_250.")
        dictionary_id = getattr(cv.aruco, dictionary, None)
        if not isinstance(dictionary_id, int):
            raise ValueError(f"Unknown ArUco dictionary: {dictionary}")
        marker_dictionary = cv.aruco.getPredefinedDictionary(dictionary_id)
        if squares_x * squares_y // 2 > len(marker_dictionary.bytesList):
            raise ValueError("The selected dictionary does not contain enough markers for this board.")
        self.squares_x = int(squares_x)
        self.squares_y = int(squares_y)
        self.square_length = float(square_length)
        self.marker_length = float(marker_length)
        self.dictionary = dictionary
        self.legacy_pattern = bool(legacy_pattern)
        self.board = cv.aruco.CharucoBoard(
            (self.squares_x, self.squares_y), self.square_length,
            self.marker_length, marker_dictionary)
        self.board.setLegacyPattern(self.legacy_pattern)
        self.detector = cv.aruco.CharucoDetector(self.board)
        self.camera_matrix = None
        self.dist_coeffs = None
        self.image_size = None
        self.rms_error = None

    def detect(self, frame: np.ndarray):
        corners, ids, _, _ = self.detector.detectBoard(frame)
        return corners, ids

    def has_usable_corners(self, corners, ids, min_corners: int = MIN_CORNERS) -> bool:
        return (corners is not None and ids is not None and
                len(corners) == len(ids) and len(ids) >= min_corners and
                not self.board.checkCharucoCornersCollinear(ids))

    def generate(self, output=DEFAULT_BOARD, pixels_per_square: int = 400,
                 margin: int = 100, force: bool = False):
        output = Path(output)
        if output.suffix.lower() != ".png":
            raise ValueError("Use a .png filename for the board image.")
        if output.exists() and not force:
            raise FileExistsError(f"Board already exists: {output}. Use --force to replace it.")
        if pixels_per_square < 100 or margin < 0:
            raise ValueError("Use at least 100 pixels per square and a nonnegative margin.")
        size = (self.squares_x * pixels_per_square + 2 * margin,
                self.squares_y * pixels_per_square + 2 * margin)
        board_image = self.board.generateImage(size, marginSize=margin, borderBits=1)
        success, encoded = cv.imencode(".png", board_image)
        if not success:
            raise OSError("OpenCV could not encode the board image.")
        # Set PNG physical pixel size so print applications can honor actual size.
        # Some print applications ignore this; measuring a printed square is essential.
        pixels_per_meter = round(pixels_per_square / self.square_length)
        data = b"pHYs" + struct.pack(">IIB", pixels_per_meter, pixels_per_meter, 1)
        chunk = struct.pack(">I", 9) + data + struct.pack(">I", zlib.crc32(data))
        png = encoded.tobytes()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(png[:33] + chunk + png[33:])
        print(f"Saved board: {output}")
        print(f"Board: {self.squares_x} x {self.squares_y} squares, {self.dictionary}, "
              f"legacy_pattern={self.legacy_pattern}")
        print(f"Square: {self.square_length * 1000:g} mm; "
              f"marker: {self.marker_length * 1000:g} mm.")
        print(f"Printed grid should measure {self.squares_x * self.square_length * 1000:g} "
              f"x {self.squares_y * self.square_length * 1000:g} mm, excluding white margins.")
        print("Print at actual size, keep flat, and measure a square; see README.md for calibration steps.")
        return output

    def calibrate(self, images=DEFAULT_IMAGES, pattern: str = "capture_*.png",
                  output=DEFAULT_CALIBRATION, min_corners: int = MIN_CORNERS,
                  min_views: int = MIN_VIEWS):
        if min_corners < MIN_CORNERS or min_views < MIN_VIEWS:
            raise ValueError(f"Require at least {MIN_CORNERS} non-collinear corners and "
                             f"{MIN_VIEWS} calibration views.")
        image_directory = Path(images)
        if not image_directory.is_dir():
            raise FileNotFoundError(f"Capture directory does not exist: {image_directory}")
        files = sorted(path for path in image_directory.glob(pattern) if path.is_file())
        if not files:
            raise ValueError(f"No images match {image_directory / pattern}. Run python -m vision.utils.capture first.")
        object_points, image_points, filenames = [], [], []
        image_size = None
        rejected = 0
        for path in files:
            frame = cv.imread(str(path))
            if frame is None:
                print(f"Rejected {path.name}: could not read image.")
                rejected += 1
                continue
            corners, ids = self.detect(frame)
            if not self.has_usable_corners(corners, ids, min_corners):
                count = 0 if ids is None else len(ids)
                print(f"Rejected {path.name}: {count} corners; need {min_corners} non-collinear corners.")
                rejected += 1
                continue
            size = (frame.shape[1], frame.shape[0])
            if image_size is not None and size != image_size:
                raise ValueError(f"Mixed capture resolutions: {path.name} is {size}, expected {image_size}. "
                                 "Use a separate directory for each camera mode and recalibrate.")
            image_size = size
            object_set, image_set = self.board.matchImagePoints(corners, ids)
            object_points.append(object_set)
            image_points.append(image_set)
            filenames.append(str(path.resolve()))
        print(f"Accepted views: {len(filenames)}; rejected views: {rejected}")
        if len(filenames) < min_views:
            raise ValueError(f"Need at least {min_views} valid views; found {len(filenames)}. "
                             "Capture 20-30 sharp views with varied positions, distances, and tilts.")
        rms, camera_matrix, dist_coeffs, rotations, translations = cv.calibrateCamera(
            object_points, image_points, image_size, None, None)
        self._validate_arrays(camera_matrix, dist_coeffs, image_size, rms)
        errors = []
        for points, observed, rotation, translation in zip(
                object_points, image_points, rotations, translations):
            projected, _ = cv.projectPoints(points, rotation, translation, camera_matrix, dist_coeffs)
            difference = observed.reshape(-1, 2) - projected.reshape(-1, 2)
            errors.append(float(np.sqrt(np.mean(np.sum(difference ** 2, axis=1)))))
        if not np.all(np.isfinite(errors)):
            raise ValueError("Calibration produced invalid reprojection errors; collect new views.")
        self.camera_matrix = camera_matrix
        self.dist_coeffs = dist_coeffs
        self.image_size = image_size
        self.rms_error = float(rms)
        output = Path(output)
        if output.suffix.lower() != ".npz":
            raise ValueError("Use a .npz filename for the camera calibration.")
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            print(f"Replacing existing calibration: {output}")
        np.savez_compressed(
            output, camera_matrix=camera_matrix, dist_coeffs=dist_coeffs,
            image_size=np.array(image_size, dtype=int), rms_error=rms,
            squares_x=self.squares_x, squares_y=self.squares_y,
            square_length=self.square_length, marker_length=self.marker_length,
            dictionary=self.dictionary, legacy_pattern=self.legacy_pattern,
            per_view_errors=np.array(errors), filenames=np.array(filenames),
            accepted_views=len(filenames), rejected_views=rejected)
        print(f"Saved calibration: {output}")
        print(f"Image resolution: {image_size[0]} x {image_size[1]}")
        print(f"RMS reprojection error: {rms:.4f} pixels")
        for filename, error in zip(filenames, errors):
            if error > max(1.0, 2 * rms):
                print(f"Inspect {Path(filename).name}: {error:.4f} px reprojection error")
        if rms > 1.0:
            print("WARNING: RMS error exceeds 1 pixel. Check board dimensions/layout, flatness, "
                  "sharpness and view variety; inspect high-error images before relying on this result.")
        print("Verify straight edges live. Low reprojection error alone does not prove accuracy.")
        return output

    @staticmethod
    def _validate_arrays(camera_matrix, dist_coeffs, image_size, rms_error=None):
        matrix = np.asarray(camera_matrix)
        distortion = np.asarray(dist_coeffs)
        size = np.asarray(image_size)
        for name, array in (("camera_matrix", matrix), ("dist_coeffs", distortion),
                            ("image_size", size)):
            if array.dtype.kind not in "iuf" or not np.all(np.isfinite(array)):
                raise ValueError(f"Invalid calibration {name}: expected finite real numbers.")
        if (matrix.shape != (3, 3) or matrix[0, 0] <= 0 or matrix[1, 1] <= 0 or
                not np.allclose(matrix[2], [0, 0, 1]) or
                not np.isclose(matrix[1, 0], 0)):
            raise ValueError("Invalid calibration camera_matrix: expected a 3x3 intrinsic matrix "
                             "with positive focal lengths.")
        if (distortion.ndim not in (1, 2) or
                (distortion.ndim == 2 and 1 not in distortion.shape) or
                distortion.size not in (4, 5, 8, 12, 14)):
            raise ValueError("Invalid calibration dist_coeffs: expected 4, 5, 8, 12, or 14 coefficients.")
        if size.shape != (2,) or np.any(size <= 0) or np.any(size != np.floor(size)):
            raise ValueError("Invalid calibration image_size: expected positive integer (width, height).")
        if rms_error is not None:
            rms = np.asarray(rms_error)
            if (rms.shape != () or rms.dtype.kind not in "iuf" or
                    not np.isfinite(rms) or rms < 0):
                raise ValueError("Invalid calibration rms_error: expected one finite nonnegative number.")

    @classmethod
    def load(cls, path=DEFAULT_CALIBRATION):
        path = Path(path)
        if not path.is_file():
            raise FileNotFoundError(f"Calibration file not found: {path}. Capture board images, "
                                    "then run python -m vision.utils.calibration calibrate.")
        try:
            with np.load(path, allow_pickle=False) as data:
                required = ("camera_matrix", "dist_coeffs", "image_size")
                missing = [key for key in required if key not in data]
                if missing:
                    raise ValueError(f"Missing calibration fields: {', '.join(missing)}")
                matrix, distortion, size = (data[key] for key in required)
                rms = data["rms_error"] if "rms_error" in data else None
                cls._validate_arrays(matrix, distortion, size, rms)
                metadata = {}
                for key in ("squares_x", "squares_y", "square_length", "marker_length",
                            "dictionary", "legacy_pattern"):
                    if key in data:
                        if data[key].shape != ():
                            raise ValueError(f"Invalid scalar board metadata: {key}")
                        metadata[key] = data[key].item()
                result = cls(**metadata)
                result.camera_matrix = np.array(matrix, dtype=float)
                result.dist_coeffs = np.array(distortion, dtype=float).reshape(-1, 1)
                result.image_size = tuple(int(value) for value in size)
                result.rms_error = None if rms is None else float(rms)
                return result
        except (OSError, ValueError, TypeError, KeyError, EOFError, zipfile.BadZipFile, cv.error) as error:
            raise ValueError(f"Could not load calibration {path}: {error}") from error

    def validate_image_size(self, image_size):
        if self.image_size is None:
            raise ValueError("No camera calibration loaded.")
        if tuple(image_size) != self.image_size:
            raise ValueError(f"Frame resolution {tuple(image_size)} differs from calibration "
                             f"{self.image_size}. Use the calibrated camera mode or recalibrate "
                             "at the chosen resolution; intrinsics are not scaled automatically.")

    def verify(self, camera: str = "0", width: int = 1920, height: int = 1080,
               fps: float = 60):
        from ..stream import Stream
        if self.camera_matrix is None:
            raise ValueError("Load a calibration before verification.")
        stream = None
        try:
            stream = Stream(camera, width=width, height=height, fps=fps)
            print("Verification: raw image on left, undistorted image on right. q / Escape quits.")
            maps = None
            while True:
                frame = stream.read()
                size = (frame.shape[1], frame.shape[0])
                self.validate_image_size(size)
                if maps is None:
                    maps = cv.initUndistortRectifyMap(
                        self.camera_matrix, self.dist_coeffs, None,
                        self.camera_matrix, size, cv.CV_32FC1)
                corrected = cv.remap(frame, maps[0], maps[1], cv.INTER_LINEAR)
                preview = np.hstack((frame, corrected))
                if preview.shape[1] > 1600:
                    preview = cv.resize(preview, (1600, round(preview.shape[0] * 1600 / preview.shape[1])))
                for label, x in (("Raw", 15), ("Undistorted", preview.shape[1] // 2 + 15)):
                    cv.putText(preview, label, (x, 32), cv.FONT_HERSHEY_SIMPLEX,
                               0.8, (0, 255, 0), 2, cv.LINE_AA)
                cv.imshow("Calibration verification", preview)
                if cv.waitKey(1) & 0xFF in (ord("q"), 27):
                    break
                if cv.getWindowProperty("Calibration verification", cv.WND_PROP_VISIBLE) < 1:
                    break
        finally:
            if stream is not None:
                stream.release()
            cv.destroyAllWindows()


def add_board_arguments(parser):
    parser.add_argument("--squares-x", type=int, default=SQUARES_X, help="Number of square columns")
    parser.add_argument("--squares-y", type=int, default=SQUARES_Y, help="Number of square rows")
    parser.add_argument("--square-length", type=float, default=SQUARE_LENGTH, help="Printed square edge in meters")
    parser.add_argument("--marker-length", type=float, default=None,
                        help="Printed marker edge in meters (default: 0.6 x square length)")
    parser.add_argument("--dictionary", default=DICTIONARY, help="OpenCV dictionary name")
    parser.add_argument("--legacy-pattern", action="store_true", help="Match pre-OpenCV-4.6 boards with even row counts")


def calibration_from_args(args):
    return Calibration(squares_x=args.squares_x, squares_y=args.squares_y,
                       square_length=args.square_length, marker_length=args.marker_length,
                       dictionary=args.dictionary, legacy_pattern=args.legacy_pattern)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "capture":
        from .capture import main as capture_main
        return capture_main(argv[1:])
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate", help="Generate a printable ChArUco board PNG")
    add_board_arguments(generate)
    generate.add_argument("--output", type=Path, default=DEFAULT_BOARD)
    generate.add_argument("--force", action="store_true", help="Replace an existing board PNG")
    commands.add_parser("capture", help="Capture board images (same options as python -m vision.utils.capture)")
    calibrate = commands.add_parser("calibrate", help="Solve calibration from saved board views")
    add_board_arguments(calibrate)
    calibrate.add_argument("--images", type=Path, default=DEFAULT_IMAGES)
    calibrate.add_argument("--pattern", default="capture_*.png")
    calibrate.add_argument("--output", type=Path, default=DEFAULT_CALIBRATION)
    calibrate.add_argument("--min-corners", type=int, default=MIN_CORNERS)
    calibrate.add_argument("--min-views", type=int, default=MIN_VIEWS)
    verify = commands.add_parser("verify", help="Compare raw and undistorted live camera frames")
    verify.add_argument("--calibration", type=Path, default=DEFAULT_CALIBRATION)
    verify.add_argument("--camera", default="0")
    verify.add_argument("--width", type=int, default=1920)
    verify.add_argument("--height", type=int, default=1080)
    verify.add_argument("--fps", type=float, default=60)
    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            calibration_from_args(args).generate(args.output, force=args.force)
        elif args.command == "calibrate":
            calibration_from_args(args).calibrate(args.images, args.pattern, args.output,
                                                  args.min_corners, args.min_views)
        elif args.command == "verify":
            Calibration.load(args.calibration).verify(args.camera, args.width, args.height, args.fps)
    except (OSError, ValueError, RuntimeError, cv.error) as error:
        print(f"Calibration error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Stopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
