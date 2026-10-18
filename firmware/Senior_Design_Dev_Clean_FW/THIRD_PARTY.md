# Third-party support

- Existing ST HAL 1.11.5 / STM32CubeH7 1.12.1 sources and CMSIS remain under Drivers.
  H753 device header/startup were copied from the repository's H753 development
  project. Original vendor notices/licenses are retained.
- LwIP 2.1.2 sources (`src/core`, `src/include`, `src/netif/ethernet.c`) were copied
  from `Senior_Design_DEV_FW` in the same repository; BSD-style license is retained
  at `Middlewares/LwIP/COPYING`. No external download is needed to build the stack.
- The custom Ethernet adapter follows the STM32CubeH7 HAL RX allocation/link and
  TX-completion callback approach already used in the development project.
- Python tools use only the standard library.
