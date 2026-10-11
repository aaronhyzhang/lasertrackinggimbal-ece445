# Firmware projects

| Project | Target | Purpose |
| --- | --- | --- |
| [Senior_Design_DEV_FW](Senior_Design_DEV_FW/README.md) | NUCLEO-H753ZI / STM32H753ZIT6 | One FIT1036 motor with AS5600 and BOOSTXL-DRV8304H; analog position bring-up, then Ethernet |
| Senior_Design_FW | STM32H743ZIT6 custom PCB | Existing custom-board baseline; two-axis integration is later work |

Start with the dev project's [firmware guide](Senior_Design_DEV_FW/docs/FIRMWARE_GUIDE.md), [wiring record](Senior_Design_DEV_FW/docs/BOARD_CONFIG.md), and [validation record](Senior_Design_DEV_FW/docs/VALIDATION.md). Shared project intent, hardware review, architecture and vendor research live in [Senior_Design_FW/docs](Senior_Design_FW/docs/DEV_BOARD_VALIDATION_PLAN.md).

From `firmware/Senior_Design_DEV_FW`, use:

```powershell
python Tools/build.py --config Debug
python Tools/build.py --config Release
python Tools/test.py --cc clang
# Or for tests: python Tools/test.py --zig <path-to-zig.exe>
```

The build uses STM32CubeIDE's ARM GCC; `--toolchain <bin-directory>` supports another installation. A native compiler is required for the portable C tests. Python tools use the standard library. CubeIDE can import the dev project's `.project`; close/remove any same-named workspace project before importing a second copy.

The dev project uses hand-owned HAL initialization, with the old IOC archived for reference. It boots inhibited and requires calibration, mechanical zero and explicit arm. The initial servo uses bounded voltage effort; current FOC, CAN, two-axis control and vision integration are deferred. Bench checks must precede motor operation.

Build outputs, workspace state, runtime caches, local logs, downloaded vendor PDFs and local schematic attachments are not part of this source upload. Vendor source licenses are retained. Local validation metadata identifies the pre-upload snapshot; portable builds/tests should be rerun when source or board configuration changes.
