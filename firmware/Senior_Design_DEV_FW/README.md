# H753 motor demo firmware

For the isolated presentation/build project containing only the physical open-loop application, use [Senior_Design_OPEN_LOOP_DEMO](../Senior_Design_OPEN_LOOP_DEMO/README.md). It preserves the current bidirectional demo and keeps vendor support separate.

For a complete explanation of the current bidirectional open-loop firmware, see the [LaTeX technical guide and separate vector figures](docs/open_loop_control/README.md). It covers the motor physics, command mapping, PWM, startup/reversal, dependencies and measured code inventory.

**v0.4.1 wiring change:** INLB now comes from **PE10 / CN12 pin 47**, replacing PB0 / CN11 pin 34. Move that wire with power off before energizing the driver. See [phase-B diagnostics](docs/PHASE_B_PWM_DIAGNOSTIC.md).

Current v0.5.0 default: **bidirectional open-loop motor control using PG2 RUN and the PA3 pot**, without serial commands, encoder or Ethernet. RUN requests wait for 1.3–1.7 V continuously for 200 ms before arming. See [OPEN_LOOP_BOARD_CONTROL.md](OPEN_LOOP_BOARD_CONTROL.md).

Updated 2026-10-13. Firmware v0.5.0. Target: NUCLEO-H753ZI / STM32H753ZIT6, BOOSTXL-DRV8304H, one DFRobot FIT1036 with AS5600, 24 V motor bus.

For motor firmware (`DEMO_PWM_SCOPE_TEST=0`), change one line in `Core/Inc/demo_config.h`: `#define DEMO_DEFAULT_OPEN_LOOP 1` selects open-loop speed; change 1 to 0 for closed-loop position. Normal CubeIDE and CLI builds use that line. Current open-loop setting is 1; PG2 physical RUN owns arming while DEMO_OPEN_BOARD_CONTROL=1. Startup remains disabled.

For MCU PWM -> driver -> motor scope checks and startup timing, read [BENCH_SWITCHING_VALIDATION.md](docs/BENCH_SWITCHING_VALIDATION.md).

For staged PWM/driver/bidirectional speed validation, read the [open-loop guide](docs/OPEN_LOOP_GUIDE.md). The default uses physical RUN; optional runtime console mode selection requires DEMO_OPEN_BOARD_CONTROL=0. Explicit --profile open-loop retains the physical-control setting and boots inhibited.

Start with the [bring-up and firmware guide](docs/FIRMWARE_GUIDE.md) and [wiring/configuration record](docs/BOARD_CONFIG.md). The analog milestone comes first; Ethernet is compiled in, but is demonstrated only after the analog servo passes bench checks.

Implemented: analog signed field-speed control (0 V=-100 RPM, 1.3–1.7 V=0, 3 V=+100 RPM), 200 ms neutral startup protection and ramp/coast/re-align reversals. The selectable closed-loop path uses encoder-oriented sine PWM, bounded voltage effort, position P / velocity PI, full-turn trajectory, calibration/ZERO/ARM, ST-LINK VCP and UDP position/heartbeat/status. Current regulation, CAN, two-axis control and camera tracking are later milestones.

See [validation record](docs/VALIDATION.md) for builds/tests. The user reports complementary MCU switching and motor rotation with the preceding 100 RPM range; this bidirectional revision still needs physical reversal/startup checks and actual RPM/current/temperature measurements.

Build from this directory:

```powershell
python Tools/build.py --config Debug
python Tools/build.py --config Release
python Tools/build.py --profile open-loop --config Debug
```

CubeIDE Debug/Release configurations also include the application and pinned LwIP sources. Refresh the project in CubeIDE after these filesystem changes. The old IOC has been archived; this demo uses hand-owned initialization in `Core/Src/board.c`, not CubeMX regeneration. [Ownership details](docs/BOARD_CONFIG.md#source-and-generation-ownership).

Canonical project context remains in the H743 workspace:

- [Validation plan](../Senior_Design_FW/docs/DEV_BOARD_VALIDATION_PLAN.md)
- [Architecture](../Senior_Design_FW/docs/FIRMWARE_ARCHITECTURE.md)
- [Motor-control design](../Senior_Design_FW/docs/MOTOR_CONTROL_DESIGN.md)
- [Custom PCB reference](../Senior_Design_FW/docs/HARDWARE_REFERENCE.md)
- [Vendor research](../Senior_Design_FW/docs/ARCHITECTURE_RESEARCH.md)

Shared control math and protocol live in this project once. The H743 port should link them with a separate board implementation and sensing configuration, rather than copying this BSP.
