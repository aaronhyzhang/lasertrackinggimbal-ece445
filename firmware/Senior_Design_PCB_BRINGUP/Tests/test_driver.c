#include "drv8304.h"
#include "pcb_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint16_t reg[7]; unsigned calls,fail_at; int stuck; } device_t;
static bool transfer(void *ctx,uint16_t tx,uint16_t *rx) {
    device_t *d=ctx; ++d->calls;
    if (d->calls==d->fail_at) return false;
    if (d->stuck) { *rx=d->stuck>0?0xffff:0; return true; }
    unsigned a=(tx>>11)&15u; assert(a<7);
    *rx=d->reg[a]|0xf800u; /* Upper response bits are don't-care. */
    if ((tx&0x8000u)==0) {
        assert(a>=2);
        if (a>=3) assert((d->reg[2]&4u)!=0); /* COAST while setting gates. */
        d->reg[a]=tx&0x07ffu;
    }
    return true;
}
int main(void) {
    device_t d={0}; drv8304_snapshot_t s;
    assert(drv8304_configure(transfer,&d,&s) && s.verified);
    assert(s.read_mask==0x7f);
    assert(d.reg[2]==0x080 && d.reg[3]==(PCB_HS_SOURCE_MA==45?0x320:0x300) && d.reg[4]==0x300);
    assert(d.reg[5]==0x300 && d.reg[6]==0x280);
    unsigned count=d.calls;
    /* Exact source-current readback is required; never silently accept baseline. */
    d.reg[3]^=0x20;
    assert(!drv8304_verify(transfer,&d,&s) && s.failed_address==3);
    d.reg[3]^=0x20;d.calls=count;
    for (unsigned i=1;i<=count;++i) {
        memset(&d,0,sizeof(d)); d.fail_at=i;
        assert(!drv8304_configure(transfer,&d,&s) && !s.verified);
    }
    memset(&d,0,sizeof(d)); d.stuck=1; assert(!drv8304_configure(transfer,&d,&s));
    d.stuck=-1; assert(!drv8304_configure(transfer,&d,&s));
    for (unsigned a=0;a<7;++a) {
        memset(&d,0,sizeof(d)); assert(drv8304_configure(transfer,&d,&s));
        d.reg[a]^=1; assert(!drv8304_verify(transfer,&d,&s) && s.failed_address==a);
    }
    /* Regression: summary FAULT is in address 0, cause may exist only in 1.
     * Previously verification returned at 0 and reg[1] falsely looked zero. */
    memset(&d,0,sizeof(d)); d.reg[0]=0x400; d.reg[1]=0x040;
    bool accepted=drv8304_configure(transfer,&d,&s);
    assert(accepted==(PCB_BENCH_ALLOW_CPUV_HISTORY!=0));
    assert(s.verified==accepted && s.cpuv_history_accepted==accepted);
    assert(s.read_mask==0x7f);
    if (!accepted) assert(s.failed_address==0);
    assert(s.reg[0]==0x400 && s.reg[1]==0x040 && s.reg[6]==0x280);
    /* CPUV cannot conceal any other fault, summary-only fault, config mismatch
     * or transport failure. Status reads do not determine live nFAULT. */
    for (unsigned a=0;a<2;++a) {
        for (unsigned bit=0;bit<11;++bit) {
            if ((a==0 && bit==10) || (a==1 && bit==6)) continue;
            d.reg[0]=0x400; d.reg[1]=0x040; d.reg[a]|=(uint16_t)(1u<<bit);
            assert(!drv8304_verify(transfer,&d,&s) && !s.verified && !s.cpuv_history_accepted);
        }
    }
    d.reg[0]=0; d.reg[1]=0x040;
    assert(drv8304_verify(transfer,&d,&s)==(PCB_BENCH_ALLOW_CPUV_HISTORY!=0));
    d.reg[0]=0x400; d.reg[2]^=0x20;
    assert(!drv8304_verify(transfer,&d,&s) && !s.cpuv_history_accepted);
    d.reg[2]^=0x20;
    /* Summary without a detailed bit also stays a fault; no bit is ignored. */
    d.reg[1]=0;
    assert(!drv8304_verify(transfer,&d,&s) && !s.verified);
    assert(s.read_mask==0x7f && s.reg[0]==0x400 && s.reg[1]==0);
    /* An interrupted status capture exposes which entries are unread. */
    d.fail_at=d.calls+2;
    assert(!drv8304_verify(transfer,&d,&s) && !s.verified);
    assert(s.failed_address==1 && s.read_mask==1);
    /* Fault-time reads retain raw bits without configuration writes/clears. */
    drv8304_fault_snapshot_t fault;
    memset(&d,0,sizeof(d));d.reg[0]=0x500;d.reg[1]=0x010;
    assert(drv8304_read_faults(transfer,&d,&fault));
    assert(d.calls==2 && fault.read_mask==3 && fault.reg[0]==0x500 && fault.reg[1]==0x010);
    assert(d.reg[0]==0x500 && d.reg[1]==0x010 && d.reg[2]==0);
    for (unsigned failure=1;failure<=2;++failure) {
        d.calls=0;d.fail_at=failure;
        assert(!drv8304_read_faults(transfer,&d,&fault));
        assert(fault.read_mask==(failure==1?0:1));
        assert(fault.reg[1]==0); /* Unread entries are explicitly marked invalid. */
    }
    printf("PASS: HS source=%d mA, SPI/config/fault capture, failures and CPUV-only bench acceptance=%d; all other fault bits rejected.\n",PCB_HS_SOURCE_MA,PCB_BENCH_ALLOW_CPUV_HISTORY);
}
