# Custom PCB Ethernet audit

Reviewed 2026-10-26 against physical PDF pages 2 and 3 of
`Datasheets_UserManuals/Schematic PDF_[No Variations] (1) (1).pdf` in the original
workspace, and Jayden Kim's `c4cb1383556df1c216b0a4bbeb8e454ba25fc8a2` on
`origin/firmware-clean-start`. That branch adds `Senior_Design_Dev_Clean_FW` for
H753 Nucleo networking; it is not the custom H743 board project. Its checked PHY
IDs, DMA ownership approach and bounded receive service are useful references.
Its Nucleo GPIO mapping and vision-command application are not drop-in motor
position control. The existing DEV adapter was adapted, and the endpoint now
uses the same position controller as the pot.

| RMII signal | PCB MCU pin | Configuration |
| --- | --- | --- |
| REFCLK | PA1 | AF11, PHY input clock to MCU |
| MDIO | PA2 | AF11 |
| MDC | PC1 | AF11 |
| CRS_DV | PA7 | AF11 |
| RXD0/RXD1 | PC4/PC5 | AF11 |
| TX_EN | PB11 | AF11; Nucleo baseline used PG11 |
| TXD0 | PB12 | AF11; Nucleo baseline used PG13 |
| TXD1 | PB13 | AF11 |

U14 is LAN8742A-CZ, with Y2 a 25 MHz ABM8W crystal. PHY REFCLKO pin14 feeds
PA1 through R45=33 ohms. LED2/nINTSEL has R103=10k to GND, selecting PHY-generated
50 MHz REFCLK. Do not drive MCU MCO into this net. The shared nRST net is not an
independent software PHY-reset GPIO. Firmware starts autonegotiation through
BMCR and does not toggle/reset the MCU's reset net.

The drawn MODE0/1/2 pullups are R113/R69/R70=10k; PHYAD0 has R42=10k pulldown,
so expected PHY address is 0. Firmware also checks address1 for a strap discrepancy,
and accepts only ID1=0x0007 and ID2 model mask=0xc130. R68 is the 2.2k MDIO pullup.
R58=500 ohms belongs to the crystal circuit, not MDIO. R71 is a 10k REFCLK pullup.
RBIAS R76=12.1k; VDDCR has C74=470pF and C75=1uF. Crystal load capacitors C57/C62
are 10pF. VDDIO uses 3V3_D and analog PHY supplies use 3V3_A. The RJ45 is
RJMG1BD3B8K1ANR with integrated magnetics; transmit/receive terminations are
49.9 ohms to 3V3_A. These schematic values are not proof of fitted parts or layout.

BSR is read twice because link status is latch-low. The MAC speed/duplex follows
recognized negotiated 10/100 half/full modes. Descriptor and payload pools are
32-byte aligned in `.eth_dma` at D2 SRAM 0x30000000, with an explicit linker size
assertion. D-cache remains disabled as in the working BSP; enabling it later needs
an MPU/cache-maintenance design. DMA never references DTCM or a transient stack
payload. TX buffers remain owned until completion, and RX custom-pbuf buffers
return to the pool when freed. LwIP is foreground-only NO_SYS, software checksums,
static IPv4 192.168.10.2/24, UDP5005, bounded four-frame receive service per poll.

Hardware acceptance still required: measure 3V3_D/3V3_A and VDDCR (~1.2V), confirm
nRST release and a stable 50 MHz REFCLK at PA1, read the PHY ID/address, negotiate
link, ping, then exchange STATUS packets while outputs are inhibited. Link LEDs
alone do not verify RMII traffic. Validate under motor switching and unplug/replug.
The script's measured-position PASS is the final application test after motor
commissioning, not a substitute for these hardware checks.

References:

- [Microchip LAN8742A datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf)
- [Microchip schematic checklist](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/SupportingCollateral/LAN8742AQFNRevASchematicChecklist.pdf)
- [ST STM32CubeH7 Ethernet/LwIP example notes](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Applications/LwIP/LwIP_HTTP_Server_Socket_RTOS/readme.txt)
