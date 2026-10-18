/* Actual network.c + actual LwIP IP/UDP. Only the hardware interface/clock is faked. */
#include "network.h"
#include "network_config.h"
#include "lwip.h"
#include "lwip/init.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/udp.h"
#include "lwip/timeouts.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct netif gnetif;
static uint32_t now;
static char response[256];
static unsigned responses;
static unsigned polls;
static uint16_t response_port;
static ip4_addr_t response_ip;

u32_t sys_now(void) { return now; }
void Error_Handler(void) { fputs("LwIP assertion failed\n", stderr); abort(); }

static err_t capture(struct netif *interface, struct pbuf *packet, const ip4_addr_t *ip)
{
    (void)interface;
    uint8_t bytes[512];
    assert(packet->tot_len <= sizeof(bytes));
    assert(pbuf_copy_partial(packet, bytes, packet->tot_len, 0) == packet->tot_len);
    size_t header_length = (bytes[0] & 0x0fu) * 4u;
    assert(bytes[9] == IP_PROTO_UDP && packet->tot_len >= header_length + UDP_HLEN);
    size_t payload = packet->tot_len - header_length - UDP_HLEN;
    assert(payload < sizeof(response));
    memcpy(response, bytes + header_length + UDP_HLEN, payload);
    response[payload] = '\0';
    response_port = ((uint16_t)bytes[header_length + 2] << 8) | bytes[header_length + 3];
    response_ip = *ip;
    responses++;
    return ERR_OK;
}

static err_t fake_interface_init(struct netif *interface)
{
    interface->name[0] = 't'; interface->name[1] = 's';
    interface->mtu = 1500;
    interface->output = capture;
    return ERR_OK;
}

bool network_stack_init(void)
{
    lwip_init();
    ip4_addr_t ip, mask, gateway;
    IP4_ADDR(&ip, 192, 168, 10, 2);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);
    assert(netif_add(&gnetif, &ip, &mask, &gateway, NULL,
                     fake_interface_init, ip4_input) != NULL);
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);
    netif_set_link_up(&gnetif);
    return true;
}
void network_stack_poll(void) { polls++; sys_check_timeouts(); }
bool network_stack_link_up(void) { return netif_is_link_up(&gnetif); }

static void inject(const char *payload, size_t length, uint8_t peer, uint16_t port,
                   const char *expected)
{
    size_t total = IP_HLEN + UDP_HLEN + length;
    struct pbuf *packet = pbuf_alloc(PBUF_RAW, (u16_t)total, PBUF_RAM);
    assert(packet != NULL);
    unsigned char bytes[256] = {0};
    assert(total <= sizeof(bytes));
    struct ip_hdr *ip = (struct ip_hdr *)bytes;
    IPH_VHL_SET(ip, 4, 5);
    IPH_LEN_SET(ip, lwip_htons((u16_t)total));
    IPH_TTL_SET(ip, 64);
    IPH_PROTO_SET(ip, IP_PROTO_UDP);
    ip4_addr_t source, destination;
    IP4_ADDR(&source, 192, 168, 10, peer);
    IP4_ADDR(&destination, 192, 168, 10, 2);
    ip4_addr_copy(ip->src, source);
    ip4_addr_copy(ip->dest, destination);
    IPH_CHKSUM_SET(ip, inet_chksum(ip, IP_HLEN));
    struct udp_hdr *udp = (struct udp_hdr *)(bytes + IP_HLEN);
    udp->src = lwip_htons(port);
    udp->dest = lwip_htons(NETWORK_UDP_PORT);
    udp->len = lwip_htons((u16_t)(UDP_HLEN + length));
    /* IPv4 UDP permits zero checksum; header checksum is validated by LwIP. */
    memcpy(bytes + IP_HLEN + UDP_HLEN, payload, length);
    assert(pbuf_take(packet, bytes, (u16_t)total) == ERR_OK);
    unsigned previous = responses;
    assert(gnetif.input(packet, &gnetif) == ERR_OK);
    assert(responses == previous + 1);
    if (strcmp(response, expected) != 0) {
        fprintf(stderr, "Expected %sReceived %s", expected, response);
        abort();
    }
    assert(response_port == port && ip4_addr_cmp(&response_ip, &source));
}

static void request(const char *message, const char *expected)
{
    inject(message, strlen(message), 1, 40000, expected);
}

int main(void)
{
    assert(strcmp(network_status(), "INIT_FAILED") == 0);
    assert(network_init());
    assert(network_init()); /* Does not reinitialize an existing endpoint. */
    assert(network_link_up());
    request("HELLO\n", "HELLO\n");
    request("STATUS", "V1 STATUS 0 0 0 0 0 0 1\n");
    now = 100;
    request("V1 VISION 42 10 1 5000 -2000", "V1 STATUS 42 10 0 0 0 0 0\n");
    request("V1 VISION 42 10 1 0 0", "ERR sequence\n");
    request("V1 VISION 42 9 1 0 0", "ERR sequence\n");
    request("V1 VISION 43 11 1 0 0", "ERR session\n");
    inject("V1 VISION 42 11 1 0 0", strlen("V1 VISION 42 11 1 0 0"),
           1, 40001, "ERR owner\n");
    inject("V1 VISION 42 11 1 0 0", strlen("V1 VISION 42 11 1 0 0"),
           3, 40000, "ERR owner\n");
    now = 599;
    request("STATUS", "V1 STATUS 42 10 0 0 0 0 0\n");
    request("V1 VISION 42 11 2 0 0", "ERR syntax\n");
    now = 600;
    request("STATUS", "V1 STATUS 42 10 0 0 0 0 2\n");
    request("V2 VISION 42 11 1 0 0", "ERR version\n");
    request("V1 VISION 42 11 0 0 0", "V1 STATUS 42 11 0 0 0 0 4\n");
    request("RESET_SESSION", "OK RESET_SESSION\n");
    request("STATUS", "V1 STATUS 0 0 0 0 0 0 1\n");
    inject("V1 VISION 99 1 1 0 0", strlen("V1 VISION 99 1 1 0 0"),
           3, 40001, "V1 STATUS 99 1 0 0 0 0 0\n");
    request("", "ERR length\n");
    char oversized[129]; memset(oversized, 'x', sizeof(oversized));
    inject(oversized, sizeof(oversized), 1, 40000, "ERR length\n");
    assert(network_counters()->received == responses);
    assert(network_counters()->transmitted == responses);
    assert(network_counters()->rejected == 9);
    assert(network_counters()->send_errors == 0);
    network_poll(); assert(polls == 1);
    netif_set_link_down(&gnetif);
    assert(!network_link_up() && strcmp(network_status(), "LINK_DOWN") == 0);
    netif_set_link_up(&gnetif);
    assert(network_link_up());
    puts("PASS: real LwIP IP/UDP receive/reply, validation, ownership, timeout and reset");
    return 0;
}
