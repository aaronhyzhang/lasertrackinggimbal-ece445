"""Camera calibration with an OpenCV checkerboard (design doc 2.2.5).

Print a checkerboard and count its INNER corners (a 10x7-square board has 9x6). Then:

    py -3 -m gimbal_bridge.camcal --camera 0 --board 9x6 --square-mm 25 --out camera.json

A window shows the camera. Hold the board at different distances, tilts and image
corners. SPACE saves a view when the corners are found (aim for 15-25 views), ENTER
calibrates and saves, ESC quits. Or calibrate saved photos:

    py -3 -m gimbal_bridge.camcal --images calib_photos/*.png --board 9x6 --square-mm 25

Reprojection error (rms) under about 0.5 px is good; above 1 px, retake views.
Calibrate at the resolution you will track at (or rely on Intrinsics.scaled()).
"""
import argparse
import glob

import cv2
import numpy as np

from .camera_model import Intrinsics

CRITERIA = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)


def board_points(cols, rows, square):
    grid = np.zeros((rows * cols, 3), np.float32)
    grid[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2) * square
    return grid


def find_corners(gray, cols, rows):
    found, corners = cv2.findChessboardCorners(gray, (cols, rows),
                                               cv2.CALIB_CB_ADAPTIVE_THRESH | cv2.CALIB_CB_NORMALIZE_IMAGE)
    if not found:
        return None
    return cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), CRITERIA)


def calibrate(views, cols, rows, square, size):
    """views: list of corner arrays from find_corners(); size: (width, height)."""
    if len(views) < 5:
        raise ValueError(f'need at least 5 views with corners found, have {len(views)}')
    obj = [board_points(cols, rows, square)] * len(views)
    rms, k, dist, _, _ = cv2.calibrateCamera(obj, views, size, None, None)
    return Intrinsics(size[0], size[1], float(k[0, 0]), float(k[1, 1]), float(k[0, 2]), float(k[1, 2]),
                      [float(d) for d in dist.ravel()], float(rms))


def from_images(paths, cols, rows, square):
    views, size = [], None
    for path in paths:
        gray = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if gray is None:
            continue
        if size and (gray.shape[1], gray.shape[0]) != size:
            raise ValueError(f'{path}: all images must have the same resolution')
        size = (gray.shape[1], gray.shape[0])
        corners = find_corners(gray, cols, rows)
        if corners is not None:
            views.append(corners)
    return calibrate(views, cols, rows, square, size), len(views)


def interactive(camera, cols, rows, square, width, height):
    from .camera import CameraSource
    cam = CameraSource(camera, width, height)
    views, seq = [], 0
    try:
        while True:
            got = cam.read(seq)
            if got is None:
                continue
            frame, _, seq = got
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            corners = find_corners(gray, cols, rows)
            shown = frame.copy()
            if corners is not None:
                cv2.drawChessboardCorners(shown, (cols, rows), corners, True)
            cv2.putText(shown, f'views {len(views)}  SPACE save  ENTER calibrate  ESC quit', (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
            cv2.imshow('camcal', shown)
            key = cv2.waitKey(1) & 0xFF
            if key == 27:
                return None
            if key == 32 and corners is not None:
                views.append(corners)
            if key == 13 and len(views) >= 5:
                return calibrate(views, cols, rows, square, (frame.shape[1], frame.shape[0]))
    finally:
        cam.close()
        cv2.destroyAllWindows()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--camera', type=int, default=0)
    p.add_argument('--images', nargs='*', help='calibrate from files (glob patterns allowed)')
    p.add_argument('--board', default='9x6', help='inner corners, columns x rows')
    p.add_argument('--square-mm', type=float, default=25.0)
    p.add_argument('--width', type=int, default=640)
    p.add_argument('--height', type=int, default=480)
    p.add_argument('--out', default='camera.json')
    a = p.parse_args()
    cols, rows = (int(x) for x in a.board.lower().split('x'))
    if a.images:
        paths = sorted(f for pattern in a.images for f in glob.glob(pattern))
        k, used = from_images(paths, cols, rows, a.square_mm)
        print(f'{used}/{len(paths)} images had corners')
    else:
        k = interactive(a.camera, cols, rows, a.square_mm, a.width, a.height)
        if k is None:
            print('Cancelled.')
            return
    k.save(a.out)
    print(f'Saved {a.out}: fx={k.fx:.1f} fy={k.fy:.1f} cx={k.cx:.1f} cy={k.cy:.1f} rms={k.rms:.3f} px')


if __name__ == '__main__':
    main()
