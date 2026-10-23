#include "drv8304.h"
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
    assert(d.reg[2]==0x080 && d.reg[3]==0x300 && d.reg[4]==0x300);
    assert(d.reg[5]==0x300 && d.reg[6]==0x280);
    unsigned count=d.calls;
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
    assert(!drv8304_configure(transfer,&d,&s) && !s.verified);
    assert(s.failed_address==0 && s.read_mask==0x7f);
    assert(s.reg[0]==0x400 && s.reg[1]==0x040 && s.reg[6]==0x280);
    /* Summary without a detailed bit also stays a fault; no bit is ignored. */
    d.reg[1]=0;
    assert(!drv8304_verify(transfer,&d,&s) && !s.verified);
    assert(s.read_mask==0x7f && s.reg[0]==0x400 && s.reg[1]==0);
    /* An interrupted status capture exposes which entries are unread. */
    d.fail_at=d.calls+2;
    assert(!drv8304_verify(transfer,&d,&s) && !s.verified);
    assert(s.failed_address==1 && s.read_mask==1);
    puts("PASS: SPI framing/masks, COAST, gain/protections, full fault-status capture including summary-only faults, read-valid mask, every transfer failure and stuck MISO.");
}
