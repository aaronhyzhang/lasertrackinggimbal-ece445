"""Compile and run command-handler tests on the computer; no board needed."""

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
    with tempfile.TemporaryDirectory(prefix="gimbal-command-tests-") as directory:
        executable = pathlib.Path(directory) / "test_command_handler"
        sources = [root / "Core/Src/protocol.c", root / "Core/Src/command_handler.c",
                   root / "Tests/test_command_handler.c"]
        subprocess.run([compiler, *flags, "-I", str(root / "Core/Inc"),
                        *(str(source) for source in sources), "-o", str(executable)],
                       check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
