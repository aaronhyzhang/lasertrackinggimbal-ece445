/* TI SLVSE39B sections 7.5/7.6. S variant, 16 clocks, MSB first, SPI mode 1. */
#include "drv8304.h"
#include "pcb_config.h"
#include <string.h>
/* 6 PWM, OTW reporting, protections enabled; HS source selectable 15/45 mA;
 * HS sink and both LS currents unchanged: 30 mA sink / 15 mA LS source.
 * 4 us TDRIVE; 400 ns driver dead time; latched all-bridge OCP, VDS 0.15 V;
 * gain 20, VREF/2, sense OCP enabled at 0.25 V (NOT a 4 A shunt limit).
 * SiS890ADN Qg(max)=29 nC at 10 V: Q/I ~1.93 us at 15 mA, ~0.64 us at 45.
 * Confirm actual gate waveforms; this arithmetic does not validate layout. */
/* TI Table 16: IDRIVEP_HS bits 6:4 = 000 (15 mA), 010 (45 mA).
 * LOCK remains 011, sink bits remain 000; every protection field unchanged. */
#define HS_PROFILE (PCB_HS_SOURCE_MA==45 ? 0x320u : 0x300u)
static const uint16_t profile[7] = {0, 0, 0x080, HS_PROFILE, 0x300, 0x300, 0x280};
bool drv8304_read_faults(drv8304_transfer_t transfer, void *ctx,
                        drv8304_fault_snapshot_t *s) {
    memset(s,0,sizeof(*s));
    for (unsigned a=0;a<2;++a) {
        uint16_t rx=0;
        if (!transfer(ctx,(uint16_t)(0x8000u|(a<<11)),&rx)) return false;
        s->reg[a]=rx&0x07ffu;
        s->read_mask|=(uint8_t)(1u<<a);
    }
    return true;
}
static bool read_reg(drv8304_transfer_t transfer, void *ctx, unsigned address,
                     drv8304_snapshot_t *s) {
    uint16_t rx;
    if (!transfer(ctx, (uint16_t)(0x8000u | (address << 11)), &rx)) {
        s->failed_address=(uint8_t)address; return false;
    }
    s->reg[address]=rx & 0x07ffu;
    s->read_mask|=(uint8_t)(1u<<address);
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
    s->verified=false; s->cpuv_history_accepted=false; s->read_mask=0;
    /* Capture both status registers before rejecting a reported fault. A
     * summary-only FAULT in reg[0] can have its cause solely in reg[1].
     * Do not leave an unread, zero-initialized reg[1] looking fault-free. */
    for (unsigned a=0; a<7; ++a) {
        if (!read_reg(transfer,ctx,a,s)) return false;
    }
    /* A recovered CPUV event may leave status latched (TI 7.3.5.2).
     * Bench opt-in accepts ONLY CPUV, optionally with summary FAULT. The BSP
     * still requires nFAULT HIGH before PWM; the hardware profile is unchanged. */
    bool cpuv_only=PCB_BENCH_ALLOW_CPUV_HISTORY && s->reg[1]==0x040u &&
                   (s->reg[0]==0u || s->reg[0]==0x400u);
    for (unsigned a=0; a<7; ++a) {
        if (a<2 && cpuv_only) continue;
        if (s->reg[a] != profile[a]) { s->failed_address=(uint8_t)a; return false; }
    }
    s->cpuv_history_accepted=cpuv_only;
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
