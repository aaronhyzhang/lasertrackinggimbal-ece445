/* UDP data-only bench endpoint. No motor authorization or hardware control. */
#include "network.h"
#include "network_config.h"
#include "lwip.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "protocol.h"
#include "command_handler.h"
#include "telemetry.h"
#include <string.h>

static struct udp_pcb *endpoint;
static command_handler_t commands;
static network_counters_t counters;
static bool owner_valid;
static ip_addr_t owner_ip;
static uint16_t owner_port;

static void send_bytes(const ip_addr_t *ip, uint16_t port,
                       const char *data, size_t length)
{
    struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
    if (packet == NULL) { counters.send_errors++; return; }
    err_t result = pbuf_take(packet, data, (u16_t)length);
    if (result == ERR_OK) result = udp_sendto(endpoint, packet, ip, port);
    if (result == ERR_OK) counters.transmitted++;
    else counters.send_errors++;
    pbuf_free(packet);
}

static void send_text(const ip_addr_t *ip, uint16_t port, const char *text)
{
    send_bytes(ip, port, text, strlen(text));
}

static bool is_request(const char *bytes, size_t length, const char *request)
{
    while (length > 0 && (bytes[length - 1] == '\r' || bytes[length - 1] == '\n')) length--;
    return length == strlen(request) && memcmp(bytes, request, length) == 0;
}

static void send_status(const ip_addr_t *ip, uint16_t port)
{
    telemetry_t status;
    char response[NETWORK_REPLY_CAPACITY];
    size_t written;
    if (telemetry_collect(&commands, sys_now(), NETWORK_COMMAND_TIMEOUT_MS, &status) &&
        protocol_format_telemetry(&status, response, sizeof(response), &written) == PROTOCOL_OK) {
        send_bytes(ip, port, response, written);
    } else {
        send_text(ip, port, "ERR internal\n");
    }
}

static void received(void *argument, struct udp_pcb *pcb, struct pbuf *packet,
                     const ip_addr_t *source, u16_t port)
{
    (void)argument;
    (void)pcb;
    if (packet == NULL) return;
    /* The address may point into the packet; preserve it before freeing RX data. */
    ip_addr_t peer = *source;
    char bytes[NETWORK_MAX_DATAGRAM];
    size_t length = packet->tot_len;
    bool copied = length > 0 && length <= sizeof(bytes) &&
        pbuf_copy_partial(packet, bytes, (u16_t)length, 0) == length;
    pbuf_free(packet);
    counters.received++;
    if (!copied) {
        counters.rejected++;
        send_text(&peer, port, "ERR length\n");
        return;
    }
    if (is_request(bytes, length, "HELLO")) {
        send_bytes(&peer, port, bytes, length);
        return;
    }
    if (is_request(bytes, length, "STATUS")) {
        send_status(&peer, port);
        return;
    }
    if (is_request(bytes, length, "RESET_SESSION")) {
        /* Explicit bench-only reset: this image has no motor/arming path. */
        command_handler_init(&commands);
        owner_valid = false;
        send_text(&peer, port, "OK RESET_SESSION\n");
        return;
    }

    vision_command_t command;
    protocol_result_t parsed = protocol_parse_command(bytes, length, &command);
    if (parsed != PROTOCOL_OK) {
        counters.rejected++;
        send_text(&peer, port, parsed == PROTOCOL_UNSUPPORTED_VERSION ?
                  "ERR version\n" : "ERR syntax\n");
        return;
    }
    if (owner_valid && (!ip_addr_cmp(&peer, &owner_ip) || port != owner_port)) {
        counters.rejected++;
        send_text(&peer, port, "ERR owner\n");
        return;
    }
    command_result_t result = command_handler_accept(&commands, &command, sys_now());
    if (result != COMMAND_ACCEPTED) {
        counters.rejected++;
        send_text(&peer, port, result == COMMAND_SESSION_MISMATCH ?
                  "ERR session\n" : "ERR sequence\n");
        return;
    }
    owner_ip = peer;
    owner_port = port;
    owner_valid = true;
    send_status(&peer, port);
}

bool network_init(void)
{
    if (endpoint != NULL) return true;
    if (!network_stack_init()) return false;
    struct udp_pcb *candidate = udp_new();
    if (candidate == NULL) return false;
    if (udp_bind(candidate, IP_ADDR_ANY, NETWORK_UDP_PORT) != ERR_OK) {
        udp_remove(candidate);
        return false;
    }
    command_handler_init(&commands);
    owner_valid = false;
    endpoint = candidate;
    udp_recv(endpoint, received, NULL);
    return true;
}

void network_poll(void) { if (endpoint != NULL) network_stack_poll(); }
bool network_link_up(void) { return endpoint != NULL && network_stack_link_up(); }
const char *network_status(void)
{
    return endpoint == NULL ? "INIT_FAILED" : network_link_up() ? "LINK_UP" : "LINK_DOWN";
}
const network_counters_t *network_counters(void) { return &counters; }
