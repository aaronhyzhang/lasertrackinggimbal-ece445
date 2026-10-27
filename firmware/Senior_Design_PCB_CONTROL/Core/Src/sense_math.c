/* Pure conversions and sample interpretation; no actuation or HAL calls. */
#include "sense_math.h"
#include <math.h>
#include <string.h>
#include "thermistor_table.h"
float sense_pin_volts(uint16_t raw,float reference) {
    return isfinite(reference) && reference>=2.7f && reference<=3.6f ?
        (float)raw*(reference/65535.0f):NAN;
}
float sense_reference_volts(uint16_t raw,uint16_t factory_cal) {
    if (raw<1000u || factory_cal==0u || factory_cal==65535u) return NAN;
    float v=3.3f*(float)factory_cal/(float)raw;
    return v>=2.7f && v<=3.6f ? v:NAN;
}
float sense_current_amps(float output_v,float zero_v) {
    /* TI SLVSE39B Eq. 3: positive SP->SN shunt drop lowers SOx. */
    return (zero_v-output_v)/(20.0f*0.014f);
}
bool sense_temperature(uint16_t raw,float *ohms,float *degrees_c) {
    *ohms=*degrees_c=NAN;
    if (raw<32u || raw>65503u) return false;
    /* Divider supply and ADC VREF+ both derive from 3V3_A: ratiometric. */
    float resistance=10000.0f*(float)raw/(65535.0f-(float)raw);
    *ohms=resistance;
    if (resistance>ntc_ohms[0] || resistance<ntc_ohms[165]) return false;
    unsigned lo=0,hi=165;
    while (hi-lo>1u) {
        unsigned mid=(hi+lo)/2u;
        if (resistance>ntc_ohms[mid]) hi=mid; else lo=mid;
    }
    *degrees_c=-40.0f+(float)lo+
        (ntc_ohms[lo]-resistance)/(ntc_ohms[lo]-ntc_ohms[hi]);
    return true;
}
void sense_encoder_update(sense_unwrap_t *s,sense_encoder_t *o,
                          uint16_t raw,uint8_t status,uint32_t now_us) {
    raw&=4095u;
    bool good=(status&0x38u)==0x20u;
    o->status=status;o->magnet_ok=good;o->raw_angle=raw;
    o->angle_deg=(float)raw*(360.0f/4096.0f);
    o->sample_us=now_us;o->age_us=0;o->health_age_ms=0;++o->sample_count;
    o->valid=good;o->velocity_valid=false;
    if ((status&0x20u)==0) {
        if (s->initialized) s->continuity_lost=true;
        s->initialized=false;o->position_valid=false;o->velocity_rpm=NAN;return;
    }
    /* Weak/strong field: keep diagnostic unwrap/RPM estimates, but never mark
     * them qualified. Missing magnet still rejects numeric velocity above. */
    if (!good) s->continuity_lost=true;
    uint32_t dt=now_us-s->previous_us;
    if (!s->initialized || dt>SENSE_ENCODER_GAP_US) {
        if (s->initialized) s->continuity_lost=true;
        s->counts=raw;s->velocity_rpm=0;s->initialized=true;
    } else if (dt!=0u) {
        int32_t delta=(int32_t)raw-(int32_t)s->previous_raw;
        if (delta>2048) delta-=4096;
        if (delta< -2048) delta+=4096;
        /* An exact half turn is direction-ambiguous. */
        if (delta==2048 || delta== -2048) s->continuity_lost=true;
        s->counts+=delta;
        float rpm=(float)delta*(60000000.0f/4096.0f)/(float)dt;
        float alpha=(float)dt/(50000.0f+(float)dt); /* 50 ms velocity filter. */
        s->velocity_rpm+=alpha*(rpm-s->velocity_rpm);
        o->velocity_valid=good;
    }
    s->previous_raw=raw;s->previous_us=now_us;
    o->position_deg=(float)s->counts*(360.0f/4096.0f);
    o->velocity_rpm=s->velocity_rpm;o->position_valid=good && !s->continuity_lost;
}
void sense_encoder_age(sense_encoder_t *o,uint32_t now_us) {
    o->age_us=o->sample_count?now_us-o->sample_us:UINT32_MAX;
    o->health_age_ms=o->age_us==UINT32_MAX?UINT32_MAX:o->age_us/1000u;
    o->valid=o->sample_count!=0u && o->magnet_ok && o->age_us<=SENSE_ENCODER_MAX_AGE_US;
    if (!o->valid) o->velocity_valid=false;
}
static void zero_reset(sense_zero_t *s,uint32_t epoch) {
    sense_zero_state_t last=s->last_reject;uint8_t mask=s->reject_mask;
    float rejected[3];memcpy(rejected,s->reject_v,sizeof(rejected));
    memset(s,0,sizeof(*s));s->epoch=epoch;s->last_reject=last;s->reject_mask=mask;
    memcpy(s->reject_v,rejected,sizeof(rejected));
}
static void zero_reject(sense_zero_t *s,sense_zero_state_t reason,uint8_t mask,const float v[3]) {
    s->count=0;s->state=s->last_reject=reason;s->reject_mask=mask;
    memcpy(s->reject_v,v,sizeof(s->reject_v));
}
void sense_zero_update(sense_zero_t *s,const float v[3],
                       const sense_driver_t *d,bool valid,float reference) {
    if (s->epoch!=d->epoch || !d->awake || !d->configured || d->gain_v_per_v!=20.0f) {
        zero_reset(s,d->epoch);
    }
    if (!d->awake) { s->state=SENSE_ZERO_ASLEEP;return; }
    if (!d->configured) { s->state=SENSE_ZERO_WAIT_DRIVER;return; }
    if (d->gain_v_per_v!=20.0f) { zero_reject(s,SENSE_ZERO_WRONG_GAIN,0,v);return; }
    if (s->valid) { s->state=SENSE_ZERO_READY;return; }
    if (!valid) { zero_reject(s,SENSE_ZERO_WAIT_ADC,0,v);return; }
    if (!isfinite(reference)) { zero_reject(s,SENSE_ZERO_WAIT_REFERENCE,0,v);return; }
    if (d->pwm_active) { s->count=0;s->state=SENSE_ZERO_PWM_ACTIVE;return; }
    uint8_t rejected=0;
    for (unsigned i=0;i<3;++i) {
        if (!isfinite(v[i]) || fabsf(v[i]-reference*0.5f)>0.20f) rejected|=(uint8_t)(1u<<i);
    }
    if (rejected) { zero_reject(s,SENSE_ZERO_INPUT_RANGE,rejected,v);return; }
    if (s->count==0) {
        for (unsigned i=0;i<3;++i) { s->sum[i]=0;s->minimum[i]=s->maximum[i]=v[i]; }
    }
    for (unsigned i=0;i<3;++i) {
        s->sum[i]+=v[i];s->minimum[i]=fminf(s->minimum[i],v[i]);
        s->maximum[i]=fmaxf(s->maximum[i],v[i]);
        if (s->maximum[i]-s->minimum[i]>0.020f) rejected|=(uint8_t)(1u<<i);
    }
    if (rejected) { zero_reject(s,SENSE_ZERO_UNSTABLE,rejected,v);return; }
    s->state=SENSE_ZERO_COLLECTING;
    if (++s->count>=SENSE_ZERO_SAMPLES) {
        for (unsigned i=0;i<3;++i) s->zero[i]=(float)(s->sum[i]/s->count);
        s->valid=true;s->state=SENSE_ZERO_READY;
    }
}
