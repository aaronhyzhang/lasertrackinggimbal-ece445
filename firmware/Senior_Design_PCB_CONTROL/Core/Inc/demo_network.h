#ifndef DEMO_NETWORK_H
#define DEMO_NETWORK_H
#include <stdint.h>
typedef enum { NETWORK_MAC_INIT,NETWORK_MAC_FAILED,NETWORK_PHY_FAILED,
    NETWORK_UDP_INIT,NETWORK_LINK_DOWN,NETWORK_LINK_UP } network_state_t;
typedef struct {
    network_state_t state;
    uint32_t phy_address,phy_id1,phy_id2,bsr,special_status;
    uint32_t tx_frames,rx_frames,rx_commands,rejected;
} network_diagnostics_t;
extern volatile network_diagnostics_t g_network;
void network_init(void);
void network_poll(void);
const char *network_status(void);
#endif
