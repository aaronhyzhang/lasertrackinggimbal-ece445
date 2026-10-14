"""Run the actual open-loop algorithm and physical RUN application on a host."""
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
    output = root / "build/host-tests"
    output.mkdir(parents=True, exist_ok=True)
    compiler = [str(args.zig), "cc"] if args.zig else [args.cc]
    for name in ["test_motor", "test_app"]:
        binary = output / (name + (".exe" if sys.platform == "win32" else ""))
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror"]
        if name == "test_app":
            flags += ["-I", str(root / "Tests/fakes")]
        flags += ["-I", str(root / "Core/Inc")]
        sources = [root / f"Tests/{name}.c", root / "Core/Src/open_loop.c",
                   root / "Core/Src/modulation.c"]
        if name == "test_app":
            sources += [root / "Core/Src/demo_app.c"]
        subprocess.run([*compiler, *flags, *map(str, sources), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
