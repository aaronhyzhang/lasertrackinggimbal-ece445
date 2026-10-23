/* TI SLVSE39B sections 7.5/7.6. S variant, 16 clocks, MSB first, SPI mode 1. */
#include "drv8304.h"
#include <string.h>
/* 6 PWM, OTW reporting, protections enabled; 15 mA source / 30 mA sink;
 * 4 us TDRIVE; 400 ns driver dead time; latched all-bridge OCP, VDS 0.15 V;
 * gain 20, VREF/2, sense OCP enabled at 0.25 V (NOT a 4 A shunt limit).
 * SiS890ADN Qg(max)=29 nC at 10 V: Q/I ~1.93 us at 15 mA, below TDRIVE.
 * Confirm actual gate waveforms; this arithmetic does not validate layout. */
static const uint16_t profile[7] = {0, 0, 0x080, 0x300, 0x300, 0x300, 0x280};
static bool read_reg(drv8304_transfer_t transfer, void *ctx, unsigned address,
                     drv8304_snapshot_t *s) {
    uint16_t rx;
    if (!transfer(ctx, (uint16_t)(0x8000u | (address << 11)), &rx)) {
        s->failed_address=(uint8_t)address; return false;
    }
    s->reg[address]=rx & 0x07ffu;
    return true;
}
static bool write_check(drv8304_transfer_t transfer, void *ctx, unsigned address,
                        uint16_t value, drv8304_snapshot_t *s) {
    uint16_t rx;
    if (!transfer(ctx,(uint16_t)((address << 11) | value),&rx) ||
        !read_reg(transfer,ctx,address,s) || s->reg[address]!=value) {
        s->failed_address=(uint8_t)address; return false;
    }
    return true;
}
bool drv8304_verify(drv8304_transfer_t transfer, void *ctx, drv8304_snapshot_t *s) {
    s->verified=false;
    for (unsigned a=0; a<7; ++a) {
        if (!read_reg(transfer,ctx,a,s)) return false;
        if (s->reg[a] != profile[a]) { s->failed_address=(uint8_t)a; return false; }
    }
    s->verified=true; return true;
}
bool drv8304_configure(drv8304_transfer_t transfer, void *ctx, drv8304_snapshot_t *s) {
    memset(s,0,sizeof(*s));
    /* COAST throughout register setup. Physical inputs must already be LOW. */
    if (!write_check(transfer,ctx,2,profile[2]|0x004u,s)) return false;
    for (unsigned a=3; a<7; ++a)
        if (!write_check(transfer,ctx,a,profile[a],s)) return false;
    if (!write_check(transfer,ctx,2,profile[2],s)) return false;
    return drv8304_verify(transfer,ctx,s);
}
