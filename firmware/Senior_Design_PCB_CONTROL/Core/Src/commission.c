#include "commission.h"
#include <string.h>
#include <math.h>
commission_state_t g_commission;
static commission_sample_t trace[COMMISSION_TRACE_CAPACITY];
void commission_init(void) { memset(&g_commission,0,sizeof(g_commission));g_commission.revision=1; }
void commission_abort(void) {
    if (g_commission.prepared || g_commission.running) g_commission.aborted=true;
    g_commission.prepared=g_commission.running=g_commission.authorized=false;
}
bool commission_prepare(uint32_t mode,uint32_t amplitude,uint32_t period,uint32_t duration,
                        uint32_t centre,uint32_t radius) {
    if (g_commission.prepared || g_commission.running || mode>3 || period<20 ||
        period>10000 || duration<100 || duration>30000 || !amplitude ||
        centre<2000 || centre>358000 || radius<1000 || radius>90000 ||
        centre<radius+2000 || centre+radius>358000) return false;
    if ((mode==0 && amplitude>radius) || ((mode==1 || mode==3) && amplitude>6000) ||
        (mode==2 && amplitude>100000)) return false;
    ++g_commission.trial;g_commission.mode=mode;g_commission.amplitude=amplitude;
    g_commission.period_ms=period;g_commission.duration_ms=duration;
    g_commission.centre_mdeg=centre;g_commission.radius_mdeg=radius;
    g_commission.prepared=true;g_commission.complete=g_commission.aborted=false;
    g_commission.next_sequence=0;return true;
}
bool commission_input(control_input_t *in,const position_control_t *s) {
    if (g_commission.prepared && s->state==SERVO_RUNNING) {
        g_commission.prepared=false;g_commission.running=true;
        g_commission.started_ms=in->now_ms;
    }
    if (!g_commission.running) return false;
    uint32_t elapsed=in->now_ms-g_commission.started_ms;
    if (elapsed>=g_commission.duration_ms) {
        g_commission.running=false;g_commission.complete=true;return true;
    }
    float centre=(float)g_commission.centre_mdeg/1000;
    if (fabsf(s->position_deg-centre)>(float)g_commission.radius_mdeg/1000) {
        g_commission.running=false;g_commission.aborted=true;return false;
    }
    float sign=((elapsed/(g_commission.period_ms/2))&1u)?-1.0f:1.0f;
    if (g_commission.mode==3) {
        /* Deterministic bit mixer, independent of encoder/feedback noise. */
        uint32_t chip=elapsed/g_commission.period_ms;
        uint32_t x=chip/2u+1u;
        x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
        sign=(x&1u)?1.0f:-1.0f;
        if (chip&1u) sign=-sign; /* Every pair has zero mean voltage. */
    }
    float value=sign*(float)g_commission.amplitude/1000;
    if (g_commission.mode==0) in->target_deg=centre+value;
    else { in->experiment_mode=(uint8_t)g_commission.mode;in->experiment_value=value; }
    return false;
}
void commission_capture(const control_input_t *in,const position_control_t *s) {
    if (!g_commission.running && !g_commission.complete && !g_commission.aborted) return;
    if (!g_commission.running && g_commission.next_sequence &&
        trace[(g_commission.next_sequence-1u)%COMMISSION_TRACE_CAPACITY].state==s->state) return;
    uint32_t seq=g_commission.next_sequence;
    commission_sample_t *o=&trace[seq%COMMISSION_TRACE_CAPACITY];
    *o=(commission_sample_t){.sequence=seq,.us=in->now_us,.encoder_us=in->encoder_us,
        .config_revision=g_commission.revision,.trial=g_commission.trial,
        .position=s->position_deg,.velocity=s->velocity_rad_s,.target=in->target_deg,
        .trajectory=s->trajectory_deg,.velocity_target=s->velocity_target_rad_s,
        .velocity_error=s->velocity_error_rad_s,.p_term=s->velocity_p_v,.i_term=s->velocity_integral,
        .requested_v=s->requested_v,.applied_v=s->vq_v*position_effort_sign(s),.bus_v=in->bus_v,
        .current={in->current_a[0],in->current_a[1],in->current_a[2]},.temperature=in->temperature_c,
        .flags=(s->voltage_saturated?1u:0u)|(in->current_valid?2u:0u)|
            (in->current_window_valid?4u:0u)|(in->encoder_transport?8u:0u),
        .state=(uint16_t)s->state,.fault=(uint16_t)s->fault};
    g_commission.next_sequence=seq+1;
}
size_t commission_read(uint32_t first,void *bytes,size_t capacity) {
    /* Wire: magic, version, trial, first, count, available_next, then 88B rows. */
    if (capacity<24u) return 0;
    uint32_t next=g_commission.next_sequence;
    uint32_t oldest=next>COMMISSION_TRACE_CAPACITY?next-COMMISSION_TRACE_CAPACITY:0;
    if (first<oldest) first=oldest;
    if (first>next) first=next;
    uint32_t count=(uint32_t)((capacity-24u)/sizeof(commission_sample_t));
    if (count>8u) count=8u;
    if (count>next-first) count=next-first;
    uint32_t header[6]={0x31525443u,1,g_commission.trial,first,count,next};
    memcpy(bytes,header,sizeof(header));
    for (uint32_t i=0;i<count;++i) memcpy((uint8_t *)bytes+24u+i*sizeof(commission_sample_t),
        &trace[(first+i)%COMMISSION_TRACE_CAPACITY],sizeof(commission_sample_t));
    return 24u+count*sizeof(commission_sample_t);
}
