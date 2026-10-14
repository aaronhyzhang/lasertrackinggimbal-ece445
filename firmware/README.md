# Firmware projects

| Project | Target | Purpose |
| --- | --- | --- |
| [Senior_Design_OPEN_LOOP_DEMO](Senior_Design_OPEN_LOOP_DEMO/README.md) | NUCLEO-H753ZI / STM32H753ZIT6 | Standalone physical RUN/pot bidirectional open-loop demo and presentation code |
| [Senior_Design_DEV_FW](Senior_Design_DEV_FW/README.md) | NUCLEO-H753ZI / STM32H753ZIT6 | Full development project, including the later feedback/communications paths |
| Senior_Design_FW | STM32H743ZIT6 custom PCB | Existing custom-board baseline and project context |

For the current bench demonstration, import **Senior_Design_OPEN_LOOP_DEMO** into CubeIDE as an existing project. It is self-contained and can coexist with Senior_Design_DEV_FW. No encoder, console or network is required. The isolated source has no position-control implementation or mode switch.

The standalone copy preserves v0.5.0 behavior: PA3 maps 0 V to -100 RPM, 1.3–1.7 V to neutral and 3 V to +100 RPM. A fresh PG2 RUN request requires 200 ms of valid center input before arming; boot-high RUN never arms until a LOW/HIGH sequence. Alignment, ramps, timed disabled reversal, hardware break and watchdog remain. Its README contains import, wiring and operating instructions.

From firmware/Senior_Design_OPEN_LOOP_DEMO:

    python Tools/build.py --config Debug
    python Tools/build.py --config Release
    python Tools/test.py --cc clang

Or pass --zig <path-to-zig.exe> for portable tests. ARM GCC can be discovered from CubeIDE under C:/ST or specified with --toolchain. Native IDE output remains Debug/ and Release/; CLI output is build/Debug and build/Release. No build command flashes hardware.

The projects use hand-owned HAL initialization. The standalone extraction retains only the selected HAL/CMSIS modules and headers with vendor licenses; no libraries/sources are linked to another project. ST support lives under Drivers, the application/BSP under Core, and host utilities under Tests/Tools.

The standalone copy is intentionally separate: future open-loop fixes must be ported to both trees or re-extracted. The development project's [LaTeX explanation and separate diagrams](Senior_Design_DEV_FW/docs/open_loop_control/README.md) cover the motor physics and v0.5.0 behavior; its earlier file/count tables describe the full development tree, not the smaller standalone project. The standalone [source map](Senior_Design_OPEN_LOOP_DEMO/docs/SOURCE_MAP.md) and [validation record](Senior_Design_OPEN_LOOP_DEMO/docs/VALIDATION.md) describe the extraction.

Project intent, hardware review and research remain in [Senior_Design_FW/docs](Senior_Design_FW/docs/DEV_BOARD_VALIDATION_PLAN.md). Build outputs, local workspace state and vendor reference PDFs are excluded from source control. Hardware acceptance is separate from software build/test results.
