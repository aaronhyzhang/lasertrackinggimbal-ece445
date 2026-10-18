"""Compile and run protocol tests on the computer; no STM32 tools or board needed."""

import argparse
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="Host C compiler (default: clang)")
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable address and undefined-behavior sanitizers")
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if compiler is None:
        parser.error(f"Compiler not found: {args.cc}. On macOS, install Xcode Command Line Tools.")
    root = pathlib.Path(__file__).resolve().parents[1]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    # Temporary outputs stay outside the CubeIDE source/build trees.
    with tempfile.TemporaryDirectory(prefix="gimbal-protocol-tests-") as directory:
        executable = pathlib.Path(directory) / "test_protocol"
        command = [compiler, *flags, "-I", str(root / "Core/Inc"),
                   str(root / "Core/Src/protocol.c"), str(root / "Tests/test_protocol.c"),
                   "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
