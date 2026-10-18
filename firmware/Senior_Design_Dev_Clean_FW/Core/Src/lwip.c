/* Hand-owned NO_SYS LwIP integration; all stack work runs in foreground. */
#include "lwip.h"
#include "ethernetif.h"
#include "network_config.h"
#include "main.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

struct netif gnetif;
static bool initialized;

u32_t sys_now(void)
{
    return HAL_GetTick();
}

bool network_stack_init(void)
{
    if (initialized) return true;
    if (!ethernetif_phy_present()) return false;

    lwip_init();
    ip4_addr_t ip, mask, gateway;
    IP4_ADDR(&ip, NETWORK_IP_A, NETWORK_IP_B, NETWORK_IP_C, NETWORK_IP_D);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);
    if (netif_add(&gnetif, &ip, &mask, &gateway, NULL,
                  ethernetif_init, ethernet_input) == NULL) return false;
    netif_set_default(&gnetif);
    initialized = true;
    return true;
}

void network_stack_poll(void)
{
    if (!initialized) return;
    ethernetif_poll(&gnetif);
    sys_check_timeouts();
}

bool network_stack_link_up(void)
{
    return initialized && netif_is_up(&gnetif) && netif_is_link_up(&gnetif);
}
