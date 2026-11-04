# Third-party source record

STM32H743 CMSIS/startup/HAL are retained from the working PCB project, based on
STM32CubeH7 v1.12.1. Matching Ethernet HAL headers/sources and LwIP 2.1.2 come
from the repository's DEV firmware. Preserve the ST/Arm notices and the vendor
license files in Drivers, plus `Middlewares/LwIP/COPYING`.

The Ethernet adapter follows the STM32CubeH7 custom RX/persistent TX ownership
pattern. Jayden Kim's H753 network project at c4cb138 was reviewed for PHY checks
and integration differences; see docs/ETHERNET_AUDIT.md. The custom PCB bindings
and position endpoint are separate from its vision-command application.

No SimpleFOC or ST MCSDK implementation is linked. The portable commutation,
trajectory, controllers and Python tools in this project are project-specific.
The optional current PI path is a later commissioning stage and is disabled by
default. Python uses the standard library; Zig is an external host-test tool.
