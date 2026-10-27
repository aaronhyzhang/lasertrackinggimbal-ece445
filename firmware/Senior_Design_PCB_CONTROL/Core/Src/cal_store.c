#include "cal_store.h"
#include "control_config.h"
#include <math.h>
#include <string.h>
#define TURN 6.2831853071795864769f

/* Record layout (little-endian, 64 bytes = two 32-byte H7 flash words):
 *  0 magic   4 version(u16) 6 length(u16)   8 sequence   12 config id
 * 16 axis 0: flags(u8) direction(i8) zero_raw(u16) electrical_offset(f32)
 * 24 axis 1: same     32..59 reserved (0)   60 CRC-32 of bytes 0..59 */
static void put32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0]|p[1]<<8); }

uint32_t cal_crc32(const void *data,size_t length) {
    const uint8_t *p=data;uint32_t crc=0xFFFFFFFFu;
    while (length--) {
        crc^=*p++;
        for (unsigned k=0;k<8;++k) crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
uint32_t cal_config_id(void) {
    /* Anything that changes what a stored offset or zero means invalidates old records. */
    int32_t words[5]={(int32_t)POSITION_POLE_PAIRS,TILT_ENCODER_POSITIVE_SIGN,PAN_ENCODER_POSITIVE_SIGN,
        (int32_t)lroundf(TILT_TRAVEL_MAX_DEG*1000),(int32_t)lroundf(PAN_TRAVEL_MAX_DEG*1000)};
    return cal_crc32(words,sizeof(words));
}
void cal_pack(const cal_data_t *d,uint8_t out[CAL_RECORD_BYTES]) {
    memset(out,0,CAL_RECORD_BYTES);
    put32(out,CAL_RECORD_MAGIC);put16(out+4,CAL_RECORD_VERSION);put16(out+6,CAL_RECORD_BYTES);
    put32(out+8,d->sequence);put32(out+12,cal_config_id());
    for (unsigned i=0;i<2;++i) {
        uint8_t *a=out+16+8*i;const cal_axis_t *x=&d->axis[i];
        a[0]=(uint8_t)((x->electrical?1u:0u)|(x->zero?2u:0u));a[1]=(uint8_t)x->direction;
        put16(a+2,x->zero_raw);
        uint32_t bits;memcpy(&bits,&x->electrical_offset,4);put32(a+4,bits);
    }
    put32(out+60,cal_crc32(out,60));
}
bool cal_unpack(const uint8_t in[CAL_RECORD_BYTES],cal_data_t *d) {
    if (get32(in)!=CAL_RECORD_MAGIC || get16(in+4)!=CAL_RECORD_VERSION || get16(in+6)!=CAL_RECORD_BYTES ||
        get32(in+60)!=cal_crc32(in,60) || get32(in+12)!=cal_config_id()) return false;
    memset(d,0,sizeof(*d));d->sequence=get32(in+8);
    for (unsigned i=0;i<2;++i) {
        const uint8_t *a=in+16+8*i;cal_axis_t *x=&d->axis[i];
        uint32_t bits=get32(a+4);memcpy(&x->electrical_offset,&bits,4);
        x->direction=(int8_t)a[1];x->zero_raw=get16(a+2);
        x->electrical=(a[0]&1u) && (x->direction==1 || x->direction==-1) &&
            isfinite(x->electrical_offset) && x->electrical_offset>=0 && x->electrical_offset<TURN;
        x->zero=x->electrical && (a[0]&2u) && x->zero_raw<4096u;
    }
    return true;
}
void cal_scan(const uint8_t *sector,size_t size,cal_scan_t *scan) {
    memset(scan,0,sizeof(*scan));scan->full=true;
    for (size_t offset=0;offset+CAL_RECORD_BYTES<=size;offset+=CAL_RECORD_BYTES) {
        const uint8_t *r=sector+offset;bool erased=true;
        for (unsigned j=0;j<CAL_RECORD_BYTES && erased;++j) erased=r[j]==0xFFu;
        if (erased) { scan->full=false;scan->next_offset=offset;return; } /* Log is append-only. */
        cal_data_t d;
        /* Corrupt or other-configuration records are skipped, never trusted. */
        if (cal_unpack(r,&d) && (!scan->found || (int32_t)(d.sequence-scan->latest.sequence)>0)) {
            scan->latest=d;scan->found=true;
        }
    }
}
cal_axis_t cal_capture(const position_control_t *s) {
    cal_axis_t a={0};
    a.electrical=s->commutation_calibrated;a.direction=(int8_t)s->direction;a.electrical_offset=s->electrical_offset;
    a.zero=s->commutation_calibrated && s->zero_set;
    if (a.zero) a.zero_raw=(uint16_t)(lroundf((s->zero_turns-floorf(s->zero_turns))*4096.0f)&4095);
    return a;
}
bool cal_axis_same(const cal_axis_t *a,const cal_axis_t *b) {
    if (a->electrical!=b->electrical || a->zero!=b->zero) return false;
    if (a->electrical && (a->direction!=b->direction || a->electrical_offset!=b->electrical_offset)) return false;
    return !a->zero || a->zero_raw==b->zero_raw;
}
float cal_zero_delta_deg(uint16_t a,uint16_t b) {
    int32_t d=((int32_t)a-(int32_t)b)&4095;
    if (d>=2048) d-=4096;
    return (float)d*(360.0f/4096.0f);
}
cal_restore_t cal_restore(position_control_t *s,const cal_axis_t *a) {
    if (s->state!=SERVO_OFF || s->commutation_calibrated || !s->unwrap_started || !a->electrical) return CAL_RESTORE_NONE;
    s->direction=a->direction;s->electrical_offset=a->electrical_offset;s->commutation_calibrated=true;
    s->zero_set=false;position_stop(s); /* OFF -> READY, PWM stays off. */
    if (!a->zero || !(s->axis?CAL_RESTORE_ZERO_PAN:CAL_RESTORE_ZERO_TILT)) return CAL_RESTORE_ELECTRICAL;
    /* zero_turns = zero fraction + whole turns. Accept only if exactly one choice puts
     * the current reading inside the travel window; otherwise leave the zero unset. */
    float sign=position_coordinate_sign(s),zero=(float)a->zero_raw/4096.0f;
    float low=-CAL_ZERO_MARGIN_DEG,high=(s->axis?PAN_TRAVEL_MAX_DEG:TILT_TRAVEL_MAX_DEG)+CAL_ZERO_MARGIN_DEG;
    unsigned matches=0;float chosen_turns=0,chosen_deg=0;
    for (int k=(int)floorf(s->raw_turns-zero)-2;k<=(int)floorf(s->raw_turns-zero)+2;++k) {
        float turns=zero+(float)k,deg=sign*(s->raw_turns-turns)*360.0f;
        if (deg>=low && deg<=high) { ++matches;chosen_turns=turns;chosen_deg=deg; }
    }
    if (matches!=1) return CAL_RESTORE_ELECTRICAL;
    s->zero_turns=chosen_turns;s->position_deg=chosen_deg;s->zero_set=true;
    return CAL_RESTORE_FULL;
}
