"""Build the H753 Ethernet-only image. Never flashes a board."""

import argparse
import concurrent.futures
import os
import pathlib
import shutil
import subprocess


def run(arguments):
    completed = subprocess.run([str(arg) for arg in arguments], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode:
        raise RuntimeError(completed.stdout)
    return completed.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--toolchain", type=pathlib.Path, help="Directory containing arm-none-eabi-gcc")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    suffix = ".exe" if os.name == "nt" else ""
    if args.toolchain:
        directory = args.toolchain.resolve()
    elif shutil.which("arm-none-eabi-gcc"):
        directory = pathlib.Path(shutil.which("arm-none-eabi-gcc")).parent
    else:
        candidates = sorted(pathlib.Path("/Applications").glob(
            "STM32CubeIDE*.app/Contents/Eclipse/plugins/*gnu-tools-for-stm32*/tools/bin"))
        candidates += sorted(pathlib.Path("C:/ST").glob(
            "STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin"))
        if not candidates:
            parser.error("Pass --toolchain /path/to/arm-gcc/bin, or build in STM32CubeIDE")
        directory = candidates[-1]
    gcc = directory / ("arm-none-eabi-gcc" + suffix)
    if not gcc.is_file():
        parser.error(f"Compiler not found: {gcc}")
    cpu = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = [*cpu, "-std=gnu11", "-g3", "-O0" if args.config == "Debug" else "-O2",
             "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Wall", "-Wextra",
             "-DUSE_HAL_DRIVER", "-DUSE_PWR_LDO_SUPPLY", "-DSTM32H753xx"]
    for include in ["Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc",
                    "Drivers/STM32H7xx_HAL_Driver/Inc/Legacy", "Drivers/CMSIS/Include",
                    "Drivers/CMSIS/Device/ST/STM32H7xx/Include", "Middlewares/LwIP/src/include"]:
        flags += ["-I", str(root / include)]
    sources = sorted((root / "Core/Src").glob("*.c"))
    sources += sorted((root / "Drivers/STM32H7xx_HAL_Driver/Src").glob("*.c"))
    sources += sorted((root / "Middlewares/LwIP/src/core").rglob("*.c"))
    sources += [root / "Middlewares/LwIP/src/netif/ethernet.c",
                root / "Core/Startup/startup_stm32h753zitx.s"]
    output = root / "build" / args.config
    output.mkdir(parents=True, exist_ok=True)

    def compile_source(source):
        obj = output / "objects" / source.relative_to(root).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        strict = ["-Werror"] if source.parent == root / "Core/Src" and source.name not in {
            "system_stm32h7xx.c", "syscalls.c", "sysmem.c"} else []
        log = run([gcc, *flags, *strict, "-MMD", "-MF", obj.with_suffix(".d"),
                   "-c", source, "-o", obj])
        return obj, log

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        compiled = list(pool.map(compile_source, sources))
    for _, log in compiled:
        if log: print(log, end="")
    image = output / "Senior_Design_Dev_Clean_FW.elf"
    print(run([gcc, *cpu, "-T", root / "STM32H753ZITX_FLASH.ld",
               "--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections",
               "-Wl,--print-memory-usage", f"-Wl,-Map={image.with_suffix('.map')}",
               *(obj for obj, _ in compiled), "-Wl,--start-group", "-lc", "-lm",
               "-Wl,--end-group", "-o", image]), end="")
    for kind, extension in [("binary", "bin"), ("ihex", "hex")]:
        run([directory / ("arm-none-eabi-objcopy" + suffix), "-O", kind,
             image, image.with_suffix("." + extension)])
    print(run([directory / ("arm-none-eabi-size" + suffix), image]), end="")
    print(f"Built {image}. Not flashed.")


if __name__ == "__main__":
    main()
