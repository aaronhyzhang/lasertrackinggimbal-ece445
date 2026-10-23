#ifndef DRV8304_H
#define DRV8304_H
#include <stdbool.h>
#include <stdint.h>
typedef bool (*drv8304_transfer_t)(void *context, uint16_t tx, uint16_t *rx);
typedef struct {
    uint16_t reg[7];
    uint8_t failed_address;
    bool verified;
} drv8304_snapshot_t;
/* Caller owns ENABLE, PWM inhibition, SPI mode/timing and chip select. */
bool drv8304_configure(drv8304_transfer_t transfer, void *context,
                       drv8304_snapshot_t *snapshot);
bool drv8304_verify(drv8304_transfer_t transfer, void *context,
                    drv8304_snapshot_t *snapshot);
#endif
