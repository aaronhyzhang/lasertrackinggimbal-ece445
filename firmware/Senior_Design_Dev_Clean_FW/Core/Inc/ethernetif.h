#ifndef GIMBAL_ETHERNETIF_H
#define GIMBAL_ETHERNETIF_H

#include <stdbool.h>
#include "lwip/err.h"
#include "lwip/netif.h"

/* Explicitly initialize NOLOAD DMA storage before starting Ethernet. */
void ethernetif_prepare(void);
bool ethernetif_phy_present(void);
err_t ethernetif_init(struct netif *interface);
void ethernetif_poll(struct netif *interface);

#endif
