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
                   str(root / "Core/Src/open_loop.c"),
                   str(root / "Core/Src/demo_protocol.c"), "-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    app_executable = output / ("test_app.exe" if sys.platform == "win32" else "test_app")
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-DDEMO_OPEN_BOARD_CONTROL=0", "-I", str(root / "Tests/fakes"),
                   "-I", str(root / "Core/Inc"), str(root / "Tests/test_app.c"),
                   str(root / "Core/Src/demo_app.c"), str(root / "Core/Src/motor_control.c"),
                   str(root / "Core/Src/open_loop.c"),
                   "-lm", "-o", str(app_executable)], check=True)
    subprocess.run([str(app_executable)], check=True)
    run_executable = output / ("test_board_run.exe" if sys.platform == "win32" else "test_board_run")
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-DDEMO_OPEN_BOARD_CONTROL=1", "-DDEMO_PWM_SCOPE_TEST=0", "-DDEMO_DEFAULT_OPEN_LOOP=1",
                   "-I", str(root / "Tests/fakes"), "-I", str(root / "Core/Inc"),
                   str(root / "Tests/test_board_run.c"), str(root / "Core/Src/demo_app.c"),
                   str(root / "Core/Src/motor_control.c"), str(root / "Core/Src/open_loop.c"),
                   "-lm", "-o", str(run_executable)], check=True)
    subprocess.run([str(run_executable)], check=True)
    for name, defines in [("test_open_loop", []), ("test_pwm_scope", []), ("test_defaults", []),
                          ("test_defaults", ["-DDEMO_DEFAULT_OPEN_LOOP=0"]),
                          ("test_defaults", ["-DDEMO_DEFAULT_OPEN_LOOP=1"])]:
        label = name + ("_override_" + defines[0].split("=")[-1] if defines else "")
        binary = output / (label + (".exe" if sys.platform == "win32" else ""))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", *defines,
                       "-I", str(root / "Core/Inc"), str(root / f"Tests/{name}.c"),
                       str(root / "Core/Src/motor_control.c"), str(root / "Core/Src/open_loop.c"),
                       "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(root / "Tests"),
                    "-p", "test_*.py", "-v"], check=True)


if __name__ == "__main__":
    main()
