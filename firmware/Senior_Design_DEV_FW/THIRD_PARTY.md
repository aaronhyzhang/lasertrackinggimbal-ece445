# Third-party source record

- STM32 CMSIS/device startup and HAL: retained from the H753 project / STM32CubeH7 **v1.12.1**. UART/UARTEx/IWDG sources and matching headers were added from the same installed package. Preserve ST/Arm notices and licenses.
- LwIP **2.1.2**, as distributed in STM32CubeH7 v1.12.1: copied `src/include`, `src/core`, and `src/netif/ethernet.c`; BSD-style license at `Middlewares/LwIP/COPYING`. IPv6/TCP/DHCP/socket/netconn code is disabled by `Core/Inc/lwipopts.h`.
- Ethernet adapter uses the allocation/link/free and HAL DMA ownership pattern from ST's CubeH7 LwIP examples. This demo uses persistent copy-based TX slots and a fixed custom RX pool with D-cache disabled.
- No SimpleFOC or ST MCSDK implementation is linked. DFRobot's position example informed pole-pair count and the voltage-mode loop architecture; the demo's HAL board implementation and test suite are separate.
- Python tools use only the standard library. Host tests require an external native C compiler. The isolated Zig tool used during development is a test tool, not firmware linked into the image.

Vendor package used: `STM32Cube_FW_H7_V1.12.1` from the local STM32Cube repository. ARM GCC 13.3.1 from STM32CubeIDE 1.18.0. Keep package/compiler versions with validation records when updating dependencies.
