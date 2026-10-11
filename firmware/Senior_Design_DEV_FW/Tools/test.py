"""Run actual portable C tests and Python loopback-client tests. No motor hardware.
Pass --cc gcc/clang, or --zig path/to/zig.exe. A compiler is required, not installed here.
"""
import argparse
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--zig", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    output = root / "build" / "host-tests"
    output.mkdir(parents=True, exist_ok=True)
    compiler = [str(args.zig), "cc"] if args.zig else [args.cc]
    executable = output / ("test_control.exe" if sys.platform == "win32" else "test_control")
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "Core/Inc"),
                   str(root / "Tests/test_control.c"), str(root / "Core/Src/motor_control.c"),
                   str(root / "Core/Src/demo_protocol.c"), "-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    app_executable = output / ("test_app.exe" if sys.platform == "win32" else "test_app")
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "Tests/fakes"),
                   "-I", str(root / "Core/Inc"), str(root / "Tests/test_app.c"),
                   str(root / "Core/Src/demo_app.c"), str(root / "Core/Src/motor_control.c"),
                   "-lm", "-o", str(app_executable)], check=True)
    subprocess.run([str(app_executable)], check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(root / "Tests"),
                    "-p", "test_*.py", "-v"], check=True)


if __name__ == "__main__":
    main()
