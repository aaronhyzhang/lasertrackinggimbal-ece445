#ifndef GIMBAL_LWIP_SUPPORT_H
#define GIMBAL_LWIP_SUPPORT_H

#include <stdbool.h>
#include "lwip/netif.h"

extern struct netif gnetif;
bool network_stack_init(void);
void network_stack_poll(void);
bool network_stack_link_up(void);

#endif
