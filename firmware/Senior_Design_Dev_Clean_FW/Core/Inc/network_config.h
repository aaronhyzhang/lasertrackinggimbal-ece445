#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

/* Direct, static IPv4 bench connection. Configure the Mac as 192.168.10.1/24. */
#define NETWORK_IP_A 192
#define NETWORK_IP_B 168
#define NETWORK_IP_C 10
#define NETWORK_IP_D 2
#define NETWORK_UDP_PORT 5005u
#define NETWORK_COMMAND_TIMEOUT_MS 500u
#define NETWORK_MAX_DATAGRAM 128u
#define NETWORK_REPLY_CAPACITY 192u
#define NETWORK_LINK_POLL_MS 250u
#define NETWORK_RX_BUDGET 4u
#define NETWORK_PHY_ADDRESS 0u

#endif
