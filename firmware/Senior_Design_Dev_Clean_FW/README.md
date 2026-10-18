# H753 Ethernet data bring-up

This project targets **NUCLEO-H753ZI / STM32H753ZIT6, MB1364**. It exchanges data
with a laptop over the onboard LAN8742 Ethernet connection. It does not initialize
motors, encoders, PWM, CAN or SPI. PG4 stays LOW for the former bench-driver harness;
all replies report `angles_valid=0` and `armed=0`.

The earlier custom-PCB H743 initialization and project settings are preserved under
`docs/archive/pre-ethernet/`. Other sibling firmware projects are unchanged.

## Build and flash in STM32CubeIDE

1. File > Import > General > Existing Projects into Workspace.
2. Select this directory. Leave **Copy projects into workspace** unchecked to edit
   the Git checkout directly.
3. Select **Senior_Design_Dev_Clean_FW**, refresh it, and build Debug or Release.
   Both configurations select STM32H753xx and compile the local LwIP sources.
4. Create an STM32 C/C++ Application debug configuration for this project's ELF,
   using ST-LINK/SWD. Connect ST-LINK USB, program, and resume execution.
5. Use the Nucleo **Ethernet RJ45** port for the laptop connection. ST-LINK USB
   supplies power/debug; it is not the Ethernet connection.

The project is self-contained. The compiler must be installed through CubeIDE or
an Arm GNU toolchain; no sibling source/project links are used.

Optional CLI build, without flashing:

```sh
python3 Tools/build.py --config Debug --toolchain /path/to/arm-gcc/bin
python3 Tools/build.py --config Release --toolchain /path/to/arm-gcc/bin
```

## First laptop exchange

Configure the Mac's wired Ethernet adapter manually:

| Setting | Value |
|---|---|
| Mac IPv4 | 192.168.10.1 |
| Mask | 255.255.255.0 |
| Router | Blank |
| Board IPv4 | 192.168.10.2 |
| UDP port | 5005 |

If that subnet conflicts with another connection, change both ends; board constants
are in `Core/Inc/network_config.h`. Wi-Fi does not need to provide this connection.

From the repository root:

```sh
ping 192.168.10.2
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/udp_client.py hello
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/udp_client.py status
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/udp_client.py vision --reset-session --session 42 --count 20
```

The default vision example sends pan +5 degrees and tilt -2 degrees every 100 ms.
This is test data, not live camera integration. Use `--pan`, `--tilt`, or `--lost`
to change observations. The client uses the Python standard library only.

An example exchange is:

```text
TX: V1 VISION 42 1 1 5000 -2000
RX: V1 STATUS 42 1 0 0 0 0 0
```

The reply acknowledges the accepted command; it does not echo angular errors as
measured positions. No position sensors exist in this profile.

Yellow LD2 blinks while main runs. Green LD1 indicates negotiated link. Red LD3
indicates a fatal initialization, assertion or CPU error. No solid red is not proof
of packet exchange; verify HELLO and command replies.

## Data path and responsibilities

`main.c` initializes HAL, CPU clock, GPIO and ETH, then calls `network_init()` and
services `network_poll()` continuously. No RTOS is used.

| File | Responsibility |
|---|---|
| `network.c/.h` | UDP port, requests, peer ownership and replies |
| `protocol.c/.h` | Your existing message parser and telemetry formatter |
| `command_handler.c/.h` | Your existing session/sequence/freshness handling |
| `telemetry.c/.h` | Collect data-only status, with positions invalid and motors unarmed |
| `lwip.c/.h` | IP interface setup, stack initialization and foreground servicing |
| `ethernetif.c/.h` | ETH frame transfers, LAN8742 identification and link polling |
| `lwipopts.h`, `arch/cc.h` | Bare-metal IPv4/ARP/ICMP/UDP and platform settings |
| `stm32h7xx_hal_msp.c`, `stm32h7xx_it.c` | RMII pins/clocks and ETH/SysTick IRQs |
| `Middlewares/LwIP/` | Licensed local copy of the existing LwIP 2.1.2 library |

## Requests and telemetry flags

Each UDP datagram contains one ASCII request (optional trailing CR/LF for the
three simple requests). No transmitted NUL character is required or accepted in a
vision command. Maximum request size is 128 bytes.

- `HELLO`: echo the supplied bytes. Does not acquire ownership or refresh timeouts.
- `STATUS`: report current telemetry. Does not refresh timeouts.
- `RESET_SESSION`: explicitly clear the stored observation and peer ownership.
  This is available from any peer only because the image is permanently data-only.
  Revisit this before adding any motor authorization.
- `V1 VISION <session> <sequence> <detected> <pan_mdeg> <tilt_mdeg>`:
  parse and accept a fresh sequence from the active session/peer, then reply with
  the existing V1 STATUS format. Positive pan is right; positive tilt is up.

The first accepted vision message binds ownership to source IP, source UDP port and
session. Other peers/sessions and duplicate/older sequences are rejected. Rejections
return `ERR owner`, `ERR session`, `ERR sequence`, `ERR syntax`, `ERR version` or
`ERR length`. They do not update the accepted observation. Replies go to the
request's source port; the laptop need not listen on port 5005.

Use `--reset-session` when starting a new laptop client run (which usually has a
new source port). Cable reconnect alone does not release ownership. The 500 ms
timeout uses local receive time; it does not measure camera or network latency.

Data-only meanings of the existing decimal `fault_flags` field:

| Value | Meaning |
|---|---|
| 0 | Fresh accepted observation with a detected target |
| 1 | No command accepted since boot/session reset; session/sequence zero are placeholders |
| 2 | Last accepted observation is at least 500 ms old |
| 4 | Fresh observation reports target lost |

Stale takes precedence over target lost. These are bench observation-status bits,
not hardware-fault assignments. Future hardware faults need a shared bit allocation.
STATUS is request/reply; there is no unsolicited telemetry stream.

## Hardware configuration and regeneration

RMII uses PA1/PA2/PA7, PB13, PC1/PC4/PC5, PG11/PG13, AF11; PHY address 0.
This mapping follows ST UM2407. Verify the received board's Ethernet bridges/jumper
population if link fails. The PHY provides the RMII 50 MHz clock. CPU clock uses
HSI-derived PLL1: 200 MHz CPU, 100 MHz AHB, 50 MHz APBs, LDO/VOS1, no HSE dependency.

D-cache stays disabled. Descriptors and persistent RX/TX buffers are 32-byte aligned
in `.eth_dma` in D2 SRAM at 0x30000000. The linker also collects CubeMX's
`.RxDescripSection` and `.TxDescripSection`, with a 256 KiB bound. NOLOAD storage is
initialized explicitly before ETH startup. TX buffers remain owned until DMA
completion. Enabling D-cache requires a reviewed MPU/cache policy first.

`Senior_Design_Dev_Clean_FW.ioc` now records the H753/RMII-only hardware configuration;
the H743 original is archived. **The active integration is hand-owned HAL C.**
LwIP is supplied locally rather than generated by CubeMX. CubeMX opening/regeneration
has not been verified in this environment. Before regenerating, generate into a
temporary copy and compare pins, HSI/PLL settings, PHY reference clock, DMA storage,
MAC-address lifetime, interrupts and foreground calls. Do not assume regeneration
preserves this working-source configuration merely because the IOC is present.

ST references:
[UM2407 board manual](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf),
[NUCLEO-H753ZI](https://www.st.com/en/evaluation-tools/nucleo-h753zi.html).

## Verification

Run host tests from the repository root:

```sh
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_protocol.py --sanitize
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_command_handler.py --sanitize
python3 firmware/Senior_Design_Dev_Clean_FW/Tools/test_network.py --sanitize
```

The network tests inject actual IPv4/UDP packets into the real LwIP stack and capture
the outgoing replies. Only the physical interface and clock are simulated. They
verify HELLO, status, commands, rejection, session reset, timeout and telemetry.
They do not exercise ETH DMA, real cable negotiation, the PHY reference clock or
CubeIDE regeneration. Board flashing, ping, UDP and cable-reconnect acceptance
still require the physical Nucleo and laptop adapter.
