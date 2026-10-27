/* NO_SYS raw UDP, foreground only. DMA ownership follows STM32CubeH7 1.12.1. */
#include "demo_network.h"
#include "demo_app.h"
#include "demo_protocol.h"
#include "demo_config.h"
#include "control_config.h"
#include "board.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#define DMA_DATA __attribute__((section(".eth_dma"),aligned(32)))
#define FRAME_SIZE 1536u
#define RX_COUNT 8u
#define TX_COUNT 4u
typedef struct { struct pbuf_custom custom; uint8_t data[FRAME_SIZE] __attribute__((aligned(32))); bool used; } rx_slot_t;
typedef struct { uint8_t data[FRAME_SIZE] __attribute__((aligned(32))); bool used; } tx_slot_t;
static ETH_DMADescTypeDef rx_desc[ETH_RX_DESC_CNT] DMA_DATA;
static ETH_DMADescTypeDef tx_desc[ETH_TX_DESC_CNT] DMA_DATA;
static rx_slot_t rx_slots[RX_COUNT] DMA_DATA;
static tx_slot_t tx_slots[TX_COUNT] DMA_DATA;
static ETH_HandleTypeDef ethernet;
static struct netif interface;
static struct udp_pcb *endpoint;
static bool ready;
volatile network_diagnostics_t g_network;
static uint32_t phy_address;
static uint32_t next_link_poll;
static uint8_t mac[6];
static bool owner_valid;
static ip_addr_t owner_ip;
static uint16_t owner_port;
static uint32_t owner_session, owner_sequence, owner_ms;
u32_t sys_now(void) { return HAL_GetTick(); }
static void rx_free(struct pbuf *p) { ((rx_slot_t *)p)->used=false; }
void HAL_ETH_RxAllocateCallback(uint8_t **buffer) {
    *buffer=NULL;
    for (unsigned i=0;i<RX_COUNT;++i) if (!rx_slots[i].used) {
        rx_slots[i].used=true;
        rx_slots[i].custom.custom_free_function=rx_free;
        *buffer=rx_slots[i].data;
        if (!pbuf_alloced_custom(PBUF_RAW,0,PBUF_REF,&rx_slots[i].custom,*buffer,FRAME_SIZE)) {
            rx_slots[i].used=false;*buffer=NULL;
        }
        return;
    }
}
void HAL_ETH_RxLinkCallback(void **start,void **end,uint8_t *buffer,uint16_t length) {
    struct pbuf *p=(struct pbuf *)(buffer - offsetof(rx_slot_t,data));
    p->next=NULL; p->len=length; p->tot_len=0;
    if (*start==NULL) *start=p; else ((struct pbuf *)*end)->next=p;
    *end=p;
    for (struct pbuf *q=*start;q!=NULL;q=q->next) q->tot_len+=length;
}
void HAL_ETH_TxFreeCallback(uint32_t *packet) { ((tx_slot_t *)packet)->used=false; }
static err_t send_frame(struct netif *netif,struct pbuf *p) {
    (void)netif;
    if (p->tot_len>FRAME_SIZE) return ERR_IF;
    (void)HAL_ETH_ReleaseTxPacket(&ethernet);
    for (unsigned i=0;i<TX_COUNT;++i) if (!tx_slots[i].used) {
        tx_slot_t *slot=&tx_slots[i]; slot->used=true;
        if (pbuf_copy_partial(p,slot->data,p->tot_len,0)!=p->tot_len) { slot->used=false; return ERR_IF; }
        ETH_BufferTypeDef buffer={0}; buffer.buffer=slot->data; buffer.len=p->tot_len;
        ETH_TxPacketConfig config={0}; config.Length=p->tot_len; config.TxBuffer=&buffer; config.pData=slot;
        config.Attributes=ETH_TX_PACKETS_FEATURES_CRCPAD; config.CRCPadCtrl=ETH_CRC_PAD_INSERT;
        /* HAL copies descriptor data before returning; slot is held until DMA completion. */
        if (HAL_ETH_Transmit_IT(&ethernet,&config)==HAL_OK) { ++g_network.tx_frames;return ERR_OK; }
        slot->used=false; return ERR_MEM;
    }
    return ERR_MEM;
}
void HAL_ETH_MspInit(ETH_HandleTypeDef *h) {
    (void)h;
    GPIO_InitTypeDef g={0}; g.Mode=GPIO_MODE_AF_PP; g.Pull=GPIO_NOPULL;
    g.Speed=GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate=GPIO_AF11_ETH;
    g.Pin=GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7; HAL_GPIO_Init(GPIOA,&g);
    g.Pin=GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13; HAL_GPIO_Init(GPIOB,&g);
    g.Pin=GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5; HAL_GPIO_Init(GPIOC,&g);
    /* Custom H743 PCB: TX_EN PB11, TXD0 PB12, TXD1 PB13. */
    __HAL_RCC_ETH1MAC_CLK_ENABLE(); __HAL_RCC_ETH1TX_CLK_ENABLE(); __HAL_RCC_ETH1RX_CLK_ENABLE();
    HAL_NVIC_SetPriority(ETH_IRQn,5,0); HAL_NVIC_EnableIRQ(ETH_IRQn);
}
void ETH_IRQHandler(void) { HAL_ETH_IRQHandler(&ethernet); }
static err_t interface_init(struct netif *n) {
    n->name[0]='g'; n->name[1]='m'; n->hostname="gimbal-pcb";
    n->hwaddr_len=6; memcpy(n->hwaddr,mac,6); n->mtu=1500;
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP;
    n->output=etharp_output; n->linkoutput=send_frame; return ERR_OK;
}
static void reply(const ip_addr_t *ip,uint16_t port,const char *text) {
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,(u16_t)strlen(text),PBUF_RAM);
    if (!p) return;
    if (pbuf_take(p,text,p->tot_len)==ERR_OK) (void)udp_sendto(endpoint,p,ip,port);
    pbuf_free(p);
}
static void received(void *arg,struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *ip,u16_t port) {
    (void)arg; (void)pcb;
    char bytes[80]; net_command_t command;
    if (!p) return;
    bool parsed=p->tot_len<sizeof(bytes) &&
        pbuf_copy_partial(p,bytes,p->tot_len,0)==p->tot_len && protocol_parse(bytes,p->tot_len,&command);
    pbuf_free(p);
    if (!parsed) { ++g_network.rejected;reply(ip,port,"ERR syntax"); return; }
    ++g_network.rx_commands;
    if (command.operation==NET_DISABLE) { app_stop();owner_valid=false;reply(ip,port,"OK DISABLED"); return; }
    if (command.operation==NET_STATUS) {
        char response[640]; app_status_text(response,sizeof(response)); reply(ip,port,response); return;
    }
    app_status_t s; app_status(&s); uint32_t now=HAL_GetTick();
    if (s.source!=SOURCE_ETHERNET) { reply(ip,port,"ERR source"); return; }
    bool same=owner_valid && ip_addr_cmp(ip,&owner_ip) && port==owner_port && command.session==owner_session;
    if (!same) {
        bool inhibited=s.servo.state==SERVO_OFF || s.servo.state==SERVO_READY;
        if (command.operation!=NET_POSITION || !inhibited ||
            (owner_valid && now-owner_ms<=CONTROL_COMMAND_MAX_AGE_MS)) { reply(ip,port,"ERR owner"); return; }
        owner_ip=*ip; owner_port=port; owner_session=command.session;
        owner_valid=true; owner_sequence=command.sequence-1u;
    }
    if (!protocol_newer(command.sequence,owner_sequence)) { reply(ip,port,"ERR sequence"); return; }
    bool ok=command.operation==NET_POSITION ? app_position(SOURCE_ETHERNET,command.millidegrees) :
                                             app_heartbeat(SOURCE_ETHERNET);
    if (ok) { owner_sequence=command.sequence; owner_ms=now; }
    char response[64]; snprintf(response,sizeof(response),"%s G1 %lu %lu",ok?"OK":"ERR",
        (unsigned long)command.session,(unsigned long)command.sequence); reply(ip,port,response);
}
void network_init(void) {
    __HAL_RCC_D2SRAM1_CLK_ENABLE(); __HAL_RCC_D2SRAM2_CLK_ENABLE(); __HAL_RCC_D2SRAM3_CLK_ENABLE();
    memset(rx_desc,0,sizeof(rx_desc)); memset(tx_desc,0,sizeof(tx_desc));
    memset(rx_slots,0,sizeof(rx_slots)); memset(tx_slots,0,sizeof(tx_slots));
    uint32_t id=HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    mac[0]=0x02; mac[1]=0x47; mac[2]=(uint8_t)(id>>24); mac[3]=(uint8_t)(id>>16);
    mac[4]=(uint8_t)(id>>8); mac[5]=(uint8_t)id;
    ethernet.Instance=ETH; ethernet.Init.MACAddr=mac; ethernet.Init.MediaInterface=HAL_ETH_RMII_MODE;
    ethernet.Init.RxDesc=rx_desc; ethernet.Init.TxDesc=tx_desc; ethernet.Init.RxBuffLen=FRAME_SIZE;
    g_network.state=NETWORK_MAC_INIT;
    if (HAL_ETH_Init(&ethernet)!=HAL_OK) { g_network.state=NETWORK_MAC_FAILED;return; }
    HAL_ETH_SetMDIOClockRange(&ethernet);
    /* PHYAD0 is strapped low in the schematic. Also inspect address 1 to
     * diagnose an assembly/strap difference, checking both LAN8742 ID halves. */
    bool found=false;
    for (uint32_t address=0;address<2;++address) {
        uint32_t id1=0,id2=0;
        if (HAL_ETH_ReadPHYRegister(&ethernet,address,2,&id1)==HAL_OK &&
            HAL_ETH_ReadPHYRegister(&ethernet,address,3,&id2)==HAL_OK &&
            id1==0x0007u && (id2&0xfff0u)==0xc130u) {
            phy_address=address;g_network.phy_id1=id1;g_network.phy_id2=id2;
            g_network.phy_address=address;found=true;break;
        }
    }
    if (!found) { g_network.state=NETWORK_PHY_FAILED;return; }
    /* Shared nRST is not a spare GPIO. Preserve hardware clock straps; enable
     * and restart autonegotiation through BMCR without resetting the MCU. */
    if (HAL_ETH_WritePHYRegister(&ethernet,phy_address,0,0x1200u)!=HAL_OK) {
        g_network.state=NETWORK_PHY_FAILED;return;
    }
    g_network.state=NETWORK_UDP_INIT;
    lwip_init(); ip4_addr_t ip,mask,gw;
    IP4_ADDR(&ip,192,168,10,2); IP4_ADDR(&mask,255,255,255,0); IP4_ADDR(&gw,0,0,0,0);
    if (!netif_add(&interface,&ip,&mask,&gw,NULL,interface_init,ethernet_input)) return;
    netif_set_default(&interface);
    endpoint=udp_new(); if (!endpoint) return;
    if (udp_bind(endpoint,IP_ADDR_ANY,DEMO_UDP_PORT)!=ERR_OK) { udp_remove(endpoint); endpoint=NULL; return; }
    udp_recv(endpoint,received,NULL);ready=true;g_network.state=NETWORK_LINK_DOWN;
}
static void check_link(void) {
    uint32_t bsr=0, mode=0;
    /* BSR link is latch-low: read twice, then require completed autonegotiation. */
    bool good=HAL_ETH_ReadPHYRegister(&ethernet,phy_address,1,&bsr)==HAL_OK &&
        HAL_ETH_ReadPHYRegister(&ethernet,phy_address,1,&bsr)==HAL_OK &&
        (bsr&0x24u)==0x24u && HAL_ETH_ReadPHYRegister(&ethernet,phy_address,31,&mode)==HAL_OK &&
        (mode&0x1000u)!=0;
    g_network.bsr=bsr;g_network.special_status=mode;
    uint32_t negotiated=mode&0x1cu;
    good=good && (negotiated==0x18u || negotiated==0x08u || negotiated==0x14u || negotiated==0x04u);
    if (!good && netif_is_link_up(&interface)) {
        if (CONTROL_SOURCE_ETHERNET) app_stop();
        owner_valid=false;g_network.state=NETWORK_LINK_DOWN;
        (void)HAL_ETH_Stop_IT(&ethernet); netif_set_link_down(&interface); netif_set_down(&interface);
    } else if (good && !netif_is_link_up(&interface)) {
        ETH_MACConfigTypeDef config={0}; HAL_ETH_GetMACConfig(&ethernet,&config);
        uint32_t speed=mode&0x1cu;
        config.Speed=(speed==0x18u||speed==0x08u)?ETH_SPEED_100M:ETH_SPEED_10M;
        config.DuplexMode=(speed==0x18u||speed==0x14u)?ETH_FULLDUPLEX_MODE:ETH_HALFDUPLEX_MODE;
        if (HAL_ETH_SetMACConfig(&ethernet,&config)==HAL_OK && HAL_ETH_Start_IT(&ethernet)==HAL_OK) {
            netif_set_up(&interface);netif_set_link_up(&interface);g_network.state=NETWORK_LINK_UP;
        }
    }
}
void network_poll(void) {
    if (!ready) return;
    uint32_t now=HAL_GetTick();
    if ((int32_t)(now-next_link_poll)>=0) { next_link_poll=now+500u; check_link(); }
    (void)HAL_ETH_ReleaseTxPacket(&ethernet);
    if (netif_is_link_up(&interface)) {
        for (unsigned budget=0;budget<4u;++budget) {
            struct pbuf *p=NULL;
            if (HAL_ETH_ReadData(&ethernet,(void **)&p)!=HAL_OK || !p) break;
            ++g_network.rx_frames;
            if (interface.input(p,&interface)!=ERR_OK) pbuf_free(p);
        }
    }
    sys_check_timeouts();
    app_status_t s; app_status(&s); if (s.source!=SOURCE_ETHERNET) owner_valid=false;
}
const char *network_status(void) {
    return !ready ? "INIT_FAILED" : netif_is_link_up(&interface) ? "LINK_UP" : "LINK_DOWN";
}
