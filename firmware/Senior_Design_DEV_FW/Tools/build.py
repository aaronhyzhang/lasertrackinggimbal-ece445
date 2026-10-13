"""Build the H753 image with an explicit source manifest and configuration flags.
No flash operation. Python standard library only; ARM GCC from STM32CubeIDE.
"""
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
    parser.add_argument("--profile", choices=["configured", "closed-loop", "open-loop"], default="configured",
                        help="Default uses demo_config.h; explicit profiles select motor firmware and override its boot mode")
    parser.add_argument("--scope-test", choices=["configured", "on", "off"], default="configured",
                        help="Default follows header; on builds MCU-only ADC/PWM test; off builds motor firmware")
    parser.add_argument("--toolchain", type=pathlib.Path, help="ARM GCC bin directory")
    parser.add_argument("--project", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    project = args.project.resolve()
    if args.toolchain:
        toolchain = args.toolchain
    else:
        candidates = sorted(pathlib.Path("C:/ST").glob("STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin"))
        if not candidates:
            parser.error("Pass --toolchain with the ARM GCC bin directory")
        toolchain = candidates[-1]
    gcc = toolchain / "arm-none-eabi-gcc.exe"
    if args.profile == "configured":
        output = project / "build" / args.config
    else:
        output = project / "build" / ("OpenLoop" if args.profile == "open-loop" else "ClosedLoop") / args.config
    output.mkdir(parents=True, exist_ok=True)
    cpu = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = cpu + ["-std=gnu11", "-g3", "-O0" if args.config == "Debug" else "-O2",
                   "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-Wall", "-Wextra",
                   "-DUSE_HAL_DRIVER", "-DUSE_PWR_LDO_SUPPLY", "-DSTM32H753xx"]
    if args.profile != "configured":
        flags += [f"-DDEMO_DEFAULT_OPEN_LOOP={int(args.profile == 'open-loop')}"]
    if args.scope_test != "configured":
        flags += [f"-DDEMO_PWM_SCOPE_TEST={int(args.scope_test == 'on')}"]
    elif args.profile != "configured":
        flags += ["-DDEMO_PWM_SCOPE_TEST=0"]
    for inc in ["Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc",
                "Drivers/STM32H7xx_HAL_Driver/Inc/Legacy", "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
                "Drivers/CMSIS/Include", "Middlewares/LwIP/src/include"]:
        flags += ["-I", str(project / inc)]
    sources = sorted((project / "Core/Src").glob("*.c"))
    sources += sorted((project / "Drivers/STM32H7xx_HAL_Driver/Src").glob("*.c"))
    sources += sorted((project / "Middlewares/LwIP/src/core").rglob("*.c"))
    sources += [project / "Middlewares/LwIP/src/netif/ethernet.c",
                project / "Core/Startup/startup_stm32h753zitx.s"]

    def compile_source(source):
        relative = source.relative_to(project)
        obj = output / "objects" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        strict = ["-Werror"] if source.parent == project / "Core/Src" and source.name not in {
            "system_stm32h7xx.c", "syscalls.c", "sysmem.c"} else []
        run([gcc, *flags, *strict, "-c", source, "-o", obj])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_source, sources))
    elf = output / "Senior_Design_DEV_FW.elf"
    run([gcc, *cpu, "-T", project / "STM32H753ZITX_FLASH.ld", "--specs=nano.specs",
         "--specs=nosys.specs", "-Wl,--gc-sections", "-Wl,--print-memory-usage",
         f"-Wl,-Map={output / 'Senior_Design_DEV_FW.map'}", *objects,
         "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group", "-o", elf])
    for kind, extension in [("binary", "bin"), ("ihex", "hex")]:
        run([toolchain / "arm-none-eabi-objcopy.exe", "-O", kind, elf, elf.with_suffix(f".{extension}")])
    run([toolchain / "arm-none-eabi-size.exe", elf])
    print(f"Built {elf}. Not flashed.")


if __name__ == "__main__":
    main()
