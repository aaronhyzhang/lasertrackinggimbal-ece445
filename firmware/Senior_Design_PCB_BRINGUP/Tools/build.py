"""Build the self-contained H743 physical open-loop demo. Does not flash."""
import argparse
import concurrent.futures
import pathlib
import subprocess
import sys


def run(argv):
    result = subprocess.run([str(x) for x in argv], text=True, capture_output=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {argv[0]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--toolchain", type=pathlib.Path, help="ARM GCC bin directory")
    parser.add_argument("--stage", choices=["io", "pwm", "driver", "motor"],
                        help="Explicit CLI override; CubeIDE uses pcb_config.h")
    parser.add_argument("--axis", type=int, choices=[0, 1],
                        help="Explicit CLI axis override: 0=J3, 1=J4")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    if args.toolchain:
        toolchain = args.toolchain
    else:
        candidates = sorted(pathlib.Path("C:/ST").glob("STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin"))
        if not candidates:
            parser.error("Pass --toolchain with the ARM GCC bin directory")
        toolchain = candidates[-1]
    gcc = toolchain / "arm-none-eabi-gcc.exe"
    output = root / "build" / args.config
    if args.stage:
        output = root / "build" / args.stage.upper() / args.config
    if args.axis is not None:
        output = output.parent / f"AXIS{args.axis}" / args.config
    output.mkdir(parents=True, exist_ok=True)
    cpu = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = cpu + ["-std=gnu11", "-g3", "-O0" if args.config == "Debug" else "-O2",
                   "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Wall", "-Wextra",
                   "-DUSE_HAL_DRIVER", "-DUSE_PWR_LDO_SUPPLY", "-DSTM32H743xx"]
    if args.stage:
        flags += [f"-DPCB_STAGE={['io', 'pwm', 'driver', 'motor'].index(args.stage)}"]
    if args.axis is not None:
        flags += [f"-DPCB_TEST_AXIS={args.axis}"]
    for inc in ["Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc",
                "Drivers/STM32H7xx_HAL_Driver/Inc/Legacy", "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
                "Drivers/CMSIS/Include"]:
        flags += ["-I", str(root / inc)]
    sources = sorted((root / "Core/Src").glob("*.c"))
    sources += sorted((root / "Drivers/STM32H7xx_HAL_Driver/Src").glob("*.c"))
    sources += [root / "Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/system_stm32h7xx.c",
                root / "Core/Startup/startup_stm32h743zitx.s"]

    def compile_source(source):
        relative = source.relative_to(root)
        obj = output / "objects" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        strict = ["-Werror"] if source.parent == root / "Core/Src" else []
        run([gcc, *flags, *strict, "-MMD", "-MF", obj.with_suffix(".d"), "-c", source, "-o", obj])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_source, sources))
    elf = output / "Senior_Design_PCB_BRINGUP.elf"
    run([gcc, *cpu, "-T", root / "STM32H743ZITX_FLASH.ld", "--specs=nano.specs",
         "--specs=nosys.specs", "-Wl,--gc-sections", "-Wl,--print-memory-usage",
         f"-Wl,-Map={elf.with_suffix('.map')}", *objects,
         "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group", "-o", elf])
    for kind, extension in [("binary", "bin"), ("ihex", "hex")]:
        run([toolchain / "arm-none-eabi-objcopy.exe", "-O", kind, elf, elf.with_suffix(f".{extension}")])
    run([toolchain / "arm-none-eabi-size.exe", elf])
    print(f"Built {elf}. Not flashed.")


if __name__ == "__main__":
    main()
