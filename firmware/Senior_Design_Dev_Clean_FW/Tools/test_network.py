"""Run data-only telemetry and real-LwIP UDP tests with a simulated hardware interface."""

import argparse
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if compiler is None: parser.error("Host compiler not found")
    root = pathlib.Path(__file__).resolve().parents[1]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-g"]
    if args.sanitize: flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    includes = ["-I", str(root / "Core/Inc"), "-I", str(root / "Middlewares/LwIP/src/include")]
    common = [root / "Core/Src/command_handler.c", root / "Core/Src/protocol.c",
              root / "Core/Src/telemetry.c"]
    lwip_sources = sorted((root / "Middlewares/LwIP/src/core").rglob("*.c"))
    suites = {
        "test_telemetry": common,
        "test_network": [*common, root / "Core/Src/network.c", *lwip_sources,
                         root / "Middlewares/LwIP/src/netif/ethernet.c"],
    }
    with tempfile.TemporaryDirectory(prefix="gimbal-network-tests-") as directory:
        for name, sources in suites.items():
            executable = pathlib.Path(directory) / name
            subprocess.run([compiler, *flags, *includes, *(str(p) for p in sources),
                            str(root / f"Tests/{name}.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
