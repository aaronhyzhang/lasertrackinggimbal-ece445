/* Actual application UDP endpoint and actual LwIP IPv4/UDP. HAL is scripted.
 * Not a PHY electrical or DMA hardware test. */
#include "board.h"
#include "demo_app.h"
#include "control_config.h"
#include "demo_network.c"
#include "lwip/ip4.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/udp.h"
#include <assert.h>
#include <stdlib.h>
__declspec(dllimport) void *VirtualAlloc(void *,size_t,unsigned long,unsigned long);
static uint32_t now;
static app_status_t app;
static unsigned stopped,positions,responses;
static bool link=true;
static uint32_t target;
static char response[700];
uint32_t HAL_GetTick(void) { return now; }
uint32_t HAL_GetUIDw0(void) { return 0x12345678u; }
uint32_t HAL_GetUIDw1(void) { return 0xabcdef01u; }
uint32_t HAL_GetUIDw2(void) { return 0x456789abu; }
void Error_Handler(void) { abort(); }
void app_status(app_status_t *s) { *s=app; }
void app_status_text(char *text,size_t length) { snprintf(text,length,"S1 TEST"); }
void app_stop(void) { ++stopped;app.servo.state=SERVO_READY; }
bool app_position(command_source_t source,uint32_t mdeg) {
    assert(source==SOURCE_ETHERNET && mdeg<=360000);target=mdeg;++positions;return true;
}
bool app_heartbeat(command_source_t source) { assert(source==SOURCE_ETHERNET);return true; }
void HAL_NVIC_SetPriority(IRQn_Type i,uint32_t a,uint32_t b) { (void)i;(void)a;(void)b; }
void HAL_NVIC_EnableIRQ(IRQn_Type i) { (void)i; }
static unsigned gpio_groups;
void HAL_GPIO_Init(GPIO_TypeDef *port,const GPIO_InitTypeDef *g) {
    assert(g->Alternate==GPIO_AF11_ETH);
    if (port==GPIOA) assert(g->Pin==(GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7));
    else if (port==GPIOB) assert(g->Pin==(GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13));
    else { assert(port==GPIOC);assert(g->Pin==(GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5)); }
    ++gpio_groups;
}
HAL_StatusTypeDef HAL_ETH_Init(ETH_HandleTypeDef *h) { HAL_ETH_MspInit(h);return HAL_OK; }
void HAL_ETH_SetMDIOClockRange(ETH_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_ETH_ReadPHYRegister(ETH_HandleTypeDef *h,uint32_t address,uint32_t reg,uint32_t *v) {
    (void)h;assert(address==0);*v=reg==2?7:reg==3?0xc132:reg==1?(link?0x24:0):0x1018;return HAL_OK;
}
HAL_StatusTypeDef HAL_ETH_WritePHYRegister(const ETH_HandleTypeDef *h,uint32_t a,uint32_t r,uint32_t v) {
    (void)h;assert(a==0 && r==0 && v==0x1200);return HAL_OK;
}
HAL_StatusTypeDef HAL_ETH_GetMACConfig(const ETH_HandleTypeDef *h,ETH_MACConfigTypeDef *c) { (void)h;memset(c,0,sizeof(*c));return HAL_OK; }
HAL_StatusTypeDef HAL_ETH_SetMACConfig(ETH_HandleTypeDef *h,ETH_MACConfigTypeDef *c) {
    (void)h;assert(c->Speed==ETH_SPEED_100M && c->DuplexMode==ETH_FULLDUPLEX_MODE);return HAL_OK;
}
HAL_StatusTypeDef HAL_ETH_Start_IT(ETH_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_ETH_Stop_IT(ETH_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_ETH_ReleaseTxPacket(ETH_HandleTypeDef *h) { (void)h;return HAL_OK; }
HAL_StatusTypeDef HAL_ETH_Transmit_IT(ETH_HandleTypeDef *h,ETH_TxPacketConfig *c) { (void)h;(void)c;return HAL_OK; }
HAL_StatusTypeDef HAL_ETH_ReadData(ETH_HandleTypeDef *h,void **p) { (void)h;*p=NULL;return HAL_OK; }
void HAL_ETH_IRQHandler(ETH_HandleTypeDef *h) { (void)h; }
static err_t capture(struct netif *n,struct pbuf *p,const ip4_addr_t *ip) {
    (void)n;(void)ip;unsigned char bytes[900];
    assert(p->tot_len<sizeof(bytes));pbuf_copy_partial(p,bytes,p->tot_len,0);
    size_t header=(bytes[0]&15u)*4u;
    size_t length=p->tot_len-header-UDP_HLEN;assert(length<sizeof(response));
    memcpy(response,bytes+header+UDP_HLEN,length);response[length]=0;++responses;return ERR_OK;
}
static void inject(const char *message,uint8_t peer,uint16_t port,const char *expected,bool corrupt) {
    size_t len=strlen(message),total=IP_HLEN+UDP_HLEN+len;
    unsigned char bytes[256]={0};assert(total<=sizeof(bytes));
    struct ip_hdr *ip=(struct ip_hdr *)bytes;
    IPH_VHL_SET(ip,4,5);IPH_LEN_SET(ip,lwip_htons((u16_t)total));IPH_TTL_SET(ip,64);IPH_PROTO_SET(ip,IP_PROTO_UDP);
    ip4_addr_t source,destination;IP4_ADDR(&source,192,168,10,peer);IP4_ADDR(&destination,192,168,10,2);
    ip4_addr_copy(ip->src,source);ip4_addr_copy(ip->dest,destination);
    IPH_CHKSUM_SET(ip,inet_chksum(ip,IP_HLEN));
    if(corrupt) IPH_TTL_SET(ip,63); /* Invalid IP checksum must not reach commands. */
    struct udp_hdr *udp=(struct udp_hdr *)(bytes+IP_HLEN);
    udp->src=lwip_htons(port);udp->dest=lwip_htons(DEMO_UDP_PORT);udp->len=lwip_htons((u16_t)(UDP_HLEN+len));
    memcpy(bytes+IP_HLEN+UDP_HLEN,message,len);
    struct pbuf *p=pbuf_alloc(PBUF_RAW,(u16_t)total,PBUF_RAM);assert(p);
    assert(pbuf_take(p,bytes,(u16_t)total)==ERR_OK);
    unsigned previous=responses;assert(ip4_input(p,&interface)==ERR_OK);
    if(corrupt) { assert(responses==previous);return; }
    assert(responses==previous+1);
    if(strcmp(response,expected)) { fprintf(stderr,"Expected %s, got %s\n",expected,response);abort(); }
}
int main(void) {
    assert(VirtualAlloc((void *)0x58000000,0x30000,0x3000,4)==(void *)0x58000000);
    assert(VirtualAlloc((void *)0x1ff10000,0x10000,0x3000,4)==(void *)0x1ff10000);
    app.source=SOURCE_ETHERNET;app.servo.state=SERVO_READY;
    network_init();assert(ready && gpio_groups==3 && g_network.phy_id2==0xc132);
    network_poll();assert(g_network.state==NETWORK_LINK_UP);
    interface.output=capture;
    inject("G1 1 1 POSITION 90000",1,40000,"OK G1 1 1",false);assert(target==90000 && positions==1);
    inject("G1 1 1 POSITION 90000",1,40000,"ERR sequence",false);
    inject("G1 1 2 HEARTBEAT",1,40001,"ERR owner",false);
    inject("G1 1 2 HEARTBEAT",3,40000,"ERR owner",false);
    inject("G1 1 2 HEARTBEAT",1,40000,"OK G1 1 2",false);
    inject("G1 1 3 POSITION 360001",1,40000,"ERR syntax",false);
    inject("G1 1 3 STATUS",1,40000,"S1 TEST",false);
    inject("G1 1 3 POSITION 180000",1,40000,"",true);assert(positions==1);
    app.servo.state=SERVO_RUNNING;now=1000;
    inject("G1 2 1 POSITION 180000",2,40000,"ERR owner",false);
    app.servo.state=SERVO_READY;
    inject("G1 2 1 POSITION 180000",2,40000,"OK G1 2 1",false);
    inject("G1 9 1 DISABLE",3,40100,"OK DISABLED",false);assert(stopped==1);
    link=false;network_poll();assert(stopped==2 && g_network.state==NETWORK_LINK_DOWN);
    /* Custom RX pbuf ownership and chained lengths use the actual adapter. */
    uint8_t *a,*b;void *first=NULL,*last=NULL;
    HAL_ETH_RxAllocateCallback(&a);HAL_ETH_RxAllocateCallback(&b);assert(a && b && a!=b);
    HAL_ETH_RxLinkCallback(&first,&last,a,10);HAL_ETH_RxLinkCallback(&first,&last,b,20);
    struct pbuf *p=first;assert(p->tot_len==30 && p->next->tot_len==20);pbuf_free(p);
    assert(!rx_slots[0].used && !rx_slots[1].used);
    puts("PASS custom RMII pin map, LAN8742 ID/link, real LwIP UDP endpoint/ownership/replay/checksum/STOP, RX pbuf lifecycle");
}
