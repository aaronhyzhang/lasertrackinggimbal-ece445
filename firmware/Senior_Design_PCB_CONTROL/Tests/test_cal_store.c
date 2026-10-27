#include "cal_store.h"
#include "control_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static cal_data_t sample(uint32_t sequence) {
    cal_data_t d={0};d.sequence=sequence;
    d.axis[0]=(cal_axis_t){true,false,-1,0,1.25f};
    d.axis[1]=(cal_axis_t){true,true,1,3000,4.5f};
    return d;
}
/* Servo just after boot: OFF, uncalibrated, first encoder sample taken. */
static position_control_t booted(unsigned axis,uint16_t raw) {
    position_control_t s;position_init_axis(&s,axis);
    s.unwrap_started=true;s.raw_turns=(float)raw/4096.0f;s.previous_raw=raw;return s;
}
/* Count for a pan coordinate (sign -1) relative to a zero count. */
static uint16_t pan_raw(uint16_t zero,float deg) {
    return (uint16_t)(((int32_t)lroundf((float)zero-deg*4096.0f/360.0f))&4095);
}

static void test_record(void) {
    assert(cal_crc32("123456789",9)==0xCBF43926u); /* Standard CRC-32 check value. */
    uint8_t r[CAL_RECORD_BYTES];cal_data_t in=sample(7),out;
    cal_pack(&in,r);assert(cal_unpack(r,&out));
    assert(out.sequence==7 && out.axis[0].electrical && !out.axis[0].zero && out.axis[0].direction==-1);
    assert(out.axis[1].zero && out.axis[1].zero_raw==3000 && out.axis[1].electrical_offset==4.5f);
    for (unsigned i=0;i<CAL_RECORD_BYTES;++i) {          /* Any single flipped bit is rejected. */
        uint8_t bad[CAL_RECORD_BYTES];memcpy(bad,r,sizeof(bad));bad[i]^=0x10u;assert(!cal_unpack(bad,&out));
    }
    cal_data_t odd=sample(1);odd.axis[0].direction=0;odd.axis[1].electrical_offset=7.0f;
    cal_pack(&odd,r);assert(cal_unpack(r,&out));         /* CRC fine but values invalid: not used. */
    assert(!out.axis[0].electrical && !out.axis[1].electrical && !out.axis[1].zero);
    cal_data_t nan=sample(1);nan.axis[1].electrical_offset=NAN;cal_pack(&nan,r);
    assert(cal_unpack(r,&out) && !out.axis[1].electrical);
}

static void test_scan(void) {
    static uint8_t sector[8*CAL_RECORD_BYTES];cal_scan_t s;
    memset(sector,0xFF,sizeof(sector));cal_scan(sector,sizeof(sector),&s);
    assert(!s.found && !s.full && s.next_offset==0);
    uint32_t sequences[3]={0xFFFFFFFEu,0xFFFFFFFFu,0u};   /* Newest wins across sequence wrap. */
    for (unsigned i=0;i<3;++i) { cal_data_t d=sample(sequences[i]);d.axis[1].zero_raw=(uint16_t)(100+i);cal_pack(&d,sector+i*CAL_RECORD_BYTES); }
    sector[1*CAL_RECORD_BYTES+20]^=1u;                    /* Corrupt the middle record: skipped. */
    cal_scan(sector,sizeof(sector),&s);
    assert(s.found && s.latest.sequence==0 && s.latest.axis[1].zero_raw==102 && s.next_offset==3*CAL_RECORD_BYTES);
    for (unsigned i=3;i<8;++i) { cal_data_t d=sample(i);cal_pack(&d,sector+i*CAL_RECORD_BYTES); }
    cal_scan(sector,sizeof(sector),&s);assert(s.full && s.latest.sequence==7);
}

static void test_capture_and_compare(void) {
    position_control_t s;position_init_axis(&s,1);
    cal_axis_t a=cal_capture(&s);assert(!a.electrical && !a.zero);
    s.commutation_calibrated=true;s.direction=-1;s.electrical_offset=2.0f;s.zero_set=true;
    s.zero_turns=-1.0f+3000.0f/4096.0f;                    /* Whole turns are not part of the record. */
    a=cal_capture(&s);assert(a.electrical && a.zero && a.zero_raw==3000 && a.direction==-1);
    cal_axis_t b=a;assert(cal_axis_same(&a,&b));b.zero_raw=3001;assert(!cal_axis_same(&a,&b));
    assert(fabsf(cal_zero_delta_deg(10,4090)-16*360.0f/4096)<1e-4f);
    assert(fabsf(cal_zero_delta_deg(4090,10)+16*360.0f/4096)<1e-4f);
}

static void test_restore_pan(void) {
    cal_axis_t a={true,true,-1,3000,1.0f};
    for (float deg=0;deg<=PAN_TRAVEL_MAX_DEG;deg+=17.3f) { /* Anywhere in travel: exact position back. */
        position_control_t s=booted(1,pan_raw(3000,deg));
        assert(cal_restore(&s,&a)==CAL_RESTORE_FULL);
        assert(s.state==SERVO_READY && s.commutation_calibrated && s.zero_set && s.direction==-1);
        assert(fabsf(s.position_deg-deg)<0.1f);
    }
    position_control_t s=booted(1,pan_raw(3000,-1.0f));   /* Small margin past the stop is allowed. */
    assert(cal_restore(&s,&a)==CAL_RESTORE_FULL && fabsf(s.position_deg+1.0f)<0.1f);
    s=booted(1,pan_raw(3000,350.0f));                       /* In the unreachable gap: no zero. */
    assert(cal_restore(&s,&a)==CAL_RESTORE_ELECTRICAL && s.state==SERVO_READY && !s.zero_set);
    cal_axis_t no_zero=a;no_zero.zero=false;s=booted(1,1000);
    assert(cal_restore(&s,&no_zero)==CAL_RESTORE_ELECTRICAL && !s.zero_set);
}

static void test_restore_guards(void) {
    cal_axis_t a={true,true,1,100,1.0f};
    position_control_t s=booted(1,100);s.state=SERVO_FAULT;assert(cal_restore(&s,&a)==CAL_RESTORE_NONE);
    s=booted(1,100);s.commutation_calibrated=true;assert(cal_restore(&s,&a)==CAL_RESTORE_NONE);
    s=booted(1,100);s.unwrap_started=false;assert(cal_restore(&s,&a)==CAL_RESTORE_NONE && s.state==SERVO_OFF);
    cal_axis_t none={0};s=booted(1,100);assert(cal_restore(&s,&none)==CAL_RESTORE_NONE && !s.commutation_calibrated);
}

static void test_restore_tilt(void) {
    cal_axis_t a={true,true,1,500,3.0f};
#if CAL_RESTORE_ZERO_TILT
    /* Tilt sign follows the calibrated direction (+1). 360-degree travel: unique
     * except near the ends, where two whole-turn choices fit and nothing is restored. */
    position_control_t s=booted(0,(uint16_t)(500+90*4096/360));
    assert(cal_restore(&s,&a)==CAL_RESTORE_FULL && fabsf(s.position_deg-90)<0.1f);
    s=booted(0,(uint16_t)(500+4096/360));                  /* about 1 degree: also fits as 361 */
    assert(cal_restore(&s,&a)==CAL_RESTORE_ELECTRICAL && !s.zero_set);
    puts("PASS tilt zero restore with whole-turn ambiguity rejection");
#else
    position_control_t s=booted(0,(uint16_t)(500+90*4096/360));
    assert(cal_restore(&s,&a)==CAL_RESTORE_ELECTRICAL && !s.zero_set && s.state==SERVO_READY);
#endif
}

int main(void) {
    assert(CAL_RESTORE_ZERO_PAN && PAN_TRAVEL_MAX_DEG+2*CAL_ZERO_MARGIN_DEG<360.0f);
    test_record();test_scan();test_capture_and_compare();
    test_restore_pan();test_restore_guards();test_restore_tilt();
    puts("PASS calibration record CRC/validation, log scan, capture, pan zero restore and guards");
    return 0;
}
