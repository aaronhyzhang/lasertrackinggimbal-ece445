# Ethernet data-profile verification

2026-10-17, America/Chicago.

- ARM GCC 14.3.rel1 (14.3.1) temporary official Arm toolchain; archive SHA256 verified against Arm's published checksum. Nothing installed system-wide.
- Debug and Release cross-compilation/link passed. Custom application code builds with -Wall -Wextra -Werror. CLI tools produced ELF, BIN and HEX.
- Debug: 39,300 bytes FLASH; Release: 24,684 bytes FLASH. Both reserve 53,496 bytes in D1 and 19,264 bytes in D2.
- Link maps place Ethernet DMA storage at 0x30000000..0x30004b40; RX descriptors at 0x30004a80 and TX at 0x30004ae0. All base addresses are 32-byte aligned and within the asserted 256 KiB DMA region.
- Existing parser suite passed: 6 groups, 6,493 checks; handler suite passed: 10 groups, 112 checks, with AddressSanitizer/UndefinedBehaviorSanitizer.
- New telemetry collection and real-LwIP IPv4/UDP receive/reply integration tests passed with both sanitizers. Only hardware interface and clock are simulated.
- Four Python client format/decoder tests passed. Project XML and Python syntax checked.
- Protocol and command-handler implementations were retained unchanged.

Not verified: native CubeIDE build/import, CubeMX opening/regeneration, physical flash, PHY identity/link negotiation, RMII clock, actual DMA completion, ping, real laptop UDP and reconnect behavior. Successful cross-compilation or simulated packets do not establish hardware acceptance. No board was flashed.

The IOC is a manually prepared H753 hardware configuration reference. Do not treat it as a verified regeneration source. See the README's regeneration workflow.

Image identities from this build:

```json
{
  "Debug": {
    "elf_sha256": "50293e7ded26f3369a43b423998bd129cd76caff06c8c5031664c2b7d0949bef",
    "bin_sha256": "0393cd0455cdc6e21d711e28df9d5089fc4b3e76d06fa81f8d25a2865e39ed29"
  },
  "Release": {
    "elf_sha256": "2809969ae480fa2fe789836af3127c6e7597bd7b62843626cf887a661239ea56",
    "bin_sha256": "3fabda88cbc11437690608f276d993427d66d40b213e559670a95d0fd80d4124"
  }
}
```
