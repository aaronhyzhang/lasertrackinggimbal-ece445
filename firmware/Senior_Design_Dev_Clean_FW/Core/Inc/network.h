#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t received;
    uint32_t transmitted;
    uint32_t rejected;
    uint32_t send_errors;
} network_counters_t;

/* Call once after HAL, clocks, GPIO and MX_ETH_Init. */
bool network_init(void);
/* Call frequently in main's foreground loop; never from an interrupt. */
void network_poll(void);
bool network_link_up(void);
const char *network_status(void);
const network_counters_t *network_counters(void);

#endif
