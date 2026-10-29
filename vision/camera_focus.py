"""Inspect/lock USB webcam focus on macOS using uvc-util (not OpenCV)."""

import argparse
from pathlib import Path
import shutil
import subprocess


LOCAL_TOOL = Path(__file__).resolve().parents[1] / "Tools/bin/uvc-util"


def call_uvc(tool, *arguments):
    result = subprocess.run([str(tool), *arguments], text=True, capture_output=True, timeout=10)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "UVC control request failed")
    return result.stdout.strip()


def set_verified(tool, device, control, value):
    call_uvc(tool, "--select-by-name", device, "--set", f"{control}={value}")
    actual = call_uvc(tool, "--select-by-name", device, "--get-value", control)
    try:
        # uvc-util prints boolean controls as true/false, integer controls as numbers.
        normalized = {"false": "0", "true": "1"}.get(actual.lower(), actual)
        matches = float(normalized) == value
    except ValueError:
        matches = False
    if not matches:
        raise RuntimeError(f"{control} read-back was {actual!r}, expected {value}; focus is NOT verified")
    print(f"Verified {control}={actual} on {device}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=LOCAL_TOOL if LOCAL_TOOL.is_file() else shutil.which("uvc-util"))
    parser.add_argument("--list", action="store_true", help="List USB cameras; no settings changed")
    parser.add_argument("--device", help="Exact USB product name from --list; not the OpenCV index")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--disable-auto", action="store_true", help="Disable autofocus and verify read-back")
    mode.add_argument("--enable-auto", action="store_true", help="Restore autofocus and verify read-back")
    parser.add_argument("--focus", type=int, help="Absolute focus in device-specific integer units; also disables auto")
    args = parser.parse_args(argv)
    if args.tool is None:
        parser.error("uvc-util is not installed; see vision/APPLE_OPTIMIZATION.md")
    if args.focus is not None and (args.focus < 0 or args.enable_auto):
        parser.error("Manual focus must be nonnegative and cannot be combined with --enable-auto")
    if args.list and (args.disable_auto or args.enable_auto or args.focus is not None):
        parser.error("--list does not change camera settings")
    if not args.list and not args.device:
        parser.error("Use --list first, then --device with the exact USB product name")
    try:
        if args.list:
            print(call_uvc(args.tool, "--list-devices"))
            return 0
        # Inspect capabilities before any write; do not send blind focus commands.
        controls = call_uvc(args.tool, "--select-by-name", args.device, "--list-controls")
        print(controls)
        changing = args.disable_auto or args.enable_auto or args.focus is not None
        if changing and "auto-focus" not in controls:
            raise RuntimeError("This camera does not expose a UVC auto-focus control")
        if args.focus is not None and "focus-abs" not in controls:
            raise RuntimeError("This camera does not expose a UVC focus-abs control")
        if args.focus is not None:
            print(call_uvc(args.tool, "--select-by-name", args.device, "--show-control", "focus-abs"))
        if changing:
            set_verified(args.tool, args.device, "auto-focus", int(args.enable_auto))
        if args.focus is not None:
            set_verified(args.tool, args.device, "focus-abs", args.focus)
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Camera focus error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
