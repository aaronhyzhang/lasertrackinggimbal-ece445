/* STM32CubeH7 HAL custom-RX/copy-TX adapter. D-cache must remain disabled. */
#include "ethernetif.h"
#include "main.h"
#include "network_config.h"
#include "lwip/pbuf.h"
#include "netif/etharp.h"
#include <stddef.h>
#include <string.h>

#define DMA_DATA __attribute__((section(".eth_dma"), aligned(32)))
#define FRAME_SIZE 1536u
#define RX_SLOTS 8u
#define TX_SLOTS 4u

extern ETH_HandleTypeDef heth;
typedef struct {
    struct pbuf_custom custom;
    uint8_t data[FRAME_SIZE] __attribute__((aligned(32)));
    bool used;
} rx_slot_t;
typedef struct {
    uint8_t data[FRAME_SIZE] __attribute__((aligned(32)));
    bool used;
} tx_slot_t;

static rx_slot_t rx_slots[RX_SLOTS] DMA_DATA;
static tx_slot_t tx_slots[TX_SLOTS] DMA_DATA;
static uint32_t link_polled_ms;
static bool link_polled;

void ethernetif_prepare(void)
{
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    memset(rx_slots, 0, sizeof(rx_slots));
    memset(tx_slots, 0, sizeof(tx_slots));
    link_polled = false;
}

static void rx_free(struct pbuf *packet)
{
    ((rx_slot_t *)packet)->used = false;
}

void HAL_ETH_RxAllocateCallback(uint8_t **buffer)
{
    *buffer = NULL;
    for (unsigned i = 0; i < RX_SLOTS; i++) {
        if (!rx_slots[i].used) {
            rx_slots[i].used = true;
            rx_slots[i].custom.custom_free_function = rx_free;
            struct pbuf *p = pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF,
                &rx_slots[i].custom, rx_slots[i].data, FRAME_SIZE);
            if (p == NULL) { rx_slots[i].used = false; return; }
            *buffer = rx_slots[i].data;
            return;
        }
    }
}

void HAL_ETH_RxLinkCallback(void **start, void **end, uint8_t *buffer, uint16_t length)
{
    rx_slot_t *slot = (rx_slot_t *)(buffer - offsetof(rx_slot_t, data));
    struct pbuf *packet = &slot->custom.pbuf;
    packet->next = NULL;
    packet->len = length;
    packet->tot_len = 0;
    if (*start == NULL) *start = packet;
    else ((struct pbuf *)*end)->next = packet;
    *end = packet;
    for (struct pbuf *p = *start; p != NULL; p = p->next) p->tot_len += length;
}

void HAL_ETH_TxFreeCallback(uint32_t *packet)
{
    ((tx_slot_t *)packet)->used = false;
}

static err_t send_frame(struct netif *interface, struct pbuf *packet)
{
    (void)interface;
    if (packet->tot_len > FRAME_SIZE) return ERR_IF;
    (void)HAL_ETH_ReleaseTxPacket(&heth);
    for (unsigned i = 0; i < TX_SLOTS; i++) {
        tx_slot_t *slot = &tx_slots[i];
        if (slot->used) continue;
        slot->used = true;
        if (pbuf_copy_partial(packet, slot->data, packet->tot_len, 0) != packet->tot_len) {
            slot->used = false;
            return ERR_IF;
        }
        ETH_BufferTypeDef buffer = {0};
        buffer.buffer = slot->data;
        buffer.len = packet->tot_len;
        ETH_TxPacketConfig config = {0};
        config.Length = packet->tot_len;
        config.TxBuffer = &buffer;
        config.pData = slot;
        config.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
        config.CRCPadCtrl = ETH_CRC_PAD_INSERT;
        /* Descriptor metadata is copied by HAL; payload stays owned until completion. */
        if (HAL_ETH_Transmit_IT(&heth, &config) == HAL_OK) return ERR_OK;
        slot->used = false;
        return ERR_MEM;
    }
    return ERR_MEM;
}

bool ethernetif_phy_present(void)
{
    uint32_t id1 = 0, id2 = 0;
    HAL_ETH_SetMDIOClockRange(&heth);
    return HAL_ETH_ReadPHYRegister(&heth, NETWORK_PHY_ADDRESS, 2, &id1) == HAL_OK &&
           HAL_ETH_ReadPHYRegister(&heth, NETWORK_PHY_ADDRESS, 3, &id2) == HAL_OK &&
           id1 == 0x0007u && (id2 & 0xfff0u) == 0xc130u;
}

err_t ethernetif_init(struct netif *interface)
{
    interface->name[0] = 'g';
    interface->name[1] = 'm';
    interface->hostname = "gimbal-clean";
    interface->hwaddr_len = 6;
    memcpy(interface->hwaddr, heth.Init.MACAddr, 6);
    interface->mtu = 1500;
    interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    interface->output = etharp_output;
    interface->linkoutput = send_frame;
    return ERR_OK;
}

static void update_link(struct netif *interface)
{
    uint32_t status = 0, mode = 0;
    /* LAN8742 BSR is latch-low. Read twice and require completed autonegotiation. */
    bool up = HAL_ETH_ReadPHYRegister(&heth, NETWORK_PHY_ADDRESS, 1, &status) == HAL_OK &&
        HAL_ETH_ReadPHYRegister(&heth, NETWORK_PHY_ADDRESS, 1, &status) == HAL_OK &&
        (status & 0x24u) == 0x24u &&
        HAL_ETH_ReadPHYRegister(&heth, NETWORK_PHY_ADDRESS, 31, &mode) == HAL_OK &&
        (mode & 0x1000u) != 0;
    uint32_t negotiated = mode & 0x1cu;
    up = up && (negotiated == 0x18u || negotiated == 0x08u ||
                negotiated == 0x14u || negotiated == 0x04u);

    if (!up && netif_is_link_up(interface)) {
        (void)HAL_ETH_Stop_IT(&heth);
        netif_set_link_down(interface);
        netif_set_down(interface);
    } else if (up && !netif_is_link_up(interface)) {
        ETH_MACConfigTypeDef config = {0};
        HAL_ETH_GetMACConfig(&heth, &config);
        config.Speed = (negotiated == 0x18u || negotiated == 0x08u) ?
            ETH_SPEED_100M : ETH_SPEED_10M;
        config.DuplexMode = (negotiated == 0x18u || negotiated == 0x14u) ?
            ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
        if (HAL_ETH_SetMACConfig(&heth, &config) == HAL_OK &&
            HAL_ETH_Start_IT(&heth) == HAL_OK) {
            netif_set_up(interface);
            netif_set_link_up(interface);
        }
    }
}

void ethernetif_poll(struct netif *interface)
{
    uint32_t now = HAL_GetTick();
    if (!link_polled || now - link_polled_ms >= NETWORK_LINK_POLL_MS) {
        link_polled = true;
        link_polled_ms = now;
        update_link(interface);
    }
    (void)HAL_ETH_ReleaseTxPacket(&heth);
    if (!netif_is_link_up(interface)) return;
    for (unsigned budget = 0; budget < NETWORK_RX_BUDGET; budget++) {
        struct pbuf *packet = NULL;
        if (HAL_ETH_ReadData(&heth, (void **)&packet) != HAL_OK || packet == NULL) break;
        if (interface->input(packet, interface) != ERR_OK) pbuf_free(packet);
    }
}
