# H753 motor demo firmware

Updated 2026-10-08. Target: NUCLEO-H753ZI / STM32H753ZIT6, BOOSTXL-DRV8304H, one DFRobot FIT1036 with AS5600, 24 V motor bus.

Start with the [bring-up and firmware guide](docs/FIRMWARE_GUIDE.md) and [wiring/configuration record](docs/BOARD_CONFIG.md). The analog milestone comes first; Ethernet is compiled in, but is demonstrated only after the analog servo passes bench checks.

Implemented: encoder-oriented sine PWM with bounded voltage effort, position P / velocity PI, full-turn trajectory, timed calibration, explicit ZERO/ARM/DISABLE, fault inhibition, ST-LINK VCP console, raw UDP position/heartbeat/status and a Python laptop client. Current regulation, CAN, two-axis control and camera tracking are later milestones.

Debug and Release builds and host tests pass. No hardware has been flashed or tested. Gains, alignment strength, sampling/waveform timing and shutdown must be validated on the arriving boards. See [validation record](docs/VALIDATION.md).

Build from this directory:

```powershell
python Tools/build.py --config Debug
python Tools/build.py --config Release
```

CubeIDE Debug/Release configurations also include the application and pinned LwIP sources. Refresh the project in CubeIDE after these filesystem changes. The old IOC has been archived; this demo uses hand-owned initialization in `Core/Src/board.c`, not CubeMX regeneration. [Ownership details](docs/BOARD_CONFIG.md#source-and-generation-ownership).

Canonical project context remains in the H743 workspace:

- [Validation plan](../Senior_Design_FW/docs/DEV_BOARD_VALIDATION_PLAN.md)
- [Architecture](../Senior_Design_FW/docs/FIRMWARE_ARCHITECTURE.md)
- [Motor-control design](../Senior_Design_FW/docs/MOTOR_CONTROL_DESIGN.md)
- [Custom PCB reference](../Senior_Design_FW/docs/HARDWARE_REFERENCE.md)
- [Vendor research](../Senior_Design_FW/docs/ARCHITECTURE_RESEARCH.md)

Shared control math and protocol live in this project once. The H743 port should link them with a separate board implementation and sensing configuration, rather than copying this BSP.
