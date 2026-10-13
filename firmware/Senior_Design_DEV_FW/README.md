# H753 motor demo firmware

**v0.4.1 wiring change:** INLB now comes from **PE10 / CN12 pin 47**, replacing PB0 / CN11 pin 34. Move that wire with power off before energizing the driver. See [phase-B diagnostics](docs/PHASE_B_PWM_DIAGNOSTIC.md).

Current v0.4.1 default: **open-loop motor control using PG2 RUN and the PA3 pot**, with no serial commands, encoder or Ethernet required. Scope-test mode is now off. See [OPEN_LOOP_BOARD_CONTROL.md](docs/OPEN_LOOP_BOARD_CONTROL.md) for wiring, low-to-high RUN/zero-speed arming and fault reset.

Updated 2026-10-12. Firmware v0.4.1. Target: NUCLEO-H753ZI / STM32H753ZIT6, BOOSTXL-DRV8304H, one DFRobot FIT1036 with AS5600, 24 V motor bus.

For motor firmware (`DEMO_PWM_SCOPE_TEST=0`), change one line in `Core/Inc/demo_config.h`: `#define DEMO_DEFAULT_OPEN_LOOP 1` selects open-loop speed; change 1 to 0 for closed-loop position. Normal CubeIDE and CLI builds use that line. Current open-loop setting is 1; PG2 physical RUN owns arming while DEMO_OPEN_BOARD_CONTROL=1. Startup remains disabled.

For MCU PWM -> driver -> motor scope checks and startup timing, read [BENCH_SWITCHING_VALIDATION.md](docs/BENCH_SWITCHING_VALIDATION.md).

For staged PWM/driver/one-way analog-speed validation, start with the [open-loop guide](docs/OPEN_LOOP_GUIDE.md). Toggle modes while disabled with `MODE OPEN_LOOP` or `MODE CLOSED_LOOP`. The separate `--profile open-loop` image boots open loop, inhibited.

Start with the [bring-up and firmware guide](docs/FIRMWARE_GUIDE.md) and [wiring/configuration record](docs/BOARD_CONFIG.md). The analog milestone comes first; Ethernet is compiled in, but is demonstrated only after the analog servo passes bench checks.

Implemented: selectable encoder-free analog speed control (0–3 V -> 0–30 RPM provisional), plus encoder-oriented sine PWM with bounded voltage effort, position P / velocity PI, full-turn trajectory, timed calibration, explicit ZERO/ARM/DISABLE, fault inhibition, ST-LINK VCP console, raw UDP position/heartbeat/status and a Python laptop client. Current regulation, CAN, two-axis control and camera tracking are later milestones.

Debug and Release builds and host tests pass. The user reports successful MCU PWM/complementary switching observation; driver/gate/motor validation is still pending. Gains, alignment strength, sampling/waveform timing and shutdown must be validated on the arriving boards. See [validation record](docs/VALIDATION.md).

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
