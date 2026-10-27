/* One selected axis. 1 kHz bounded trajectory/velocity; encoder-based sine PWM.
 * Current PI is compiled as a separate, explicitly selected later stage.
 * Callers serialize tick/fast/commands. No HAL dependency: host-testable logic. */
#include "position_control.h"
#include "control_config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846f
#define TURN (2.0f*PI)
#define PP 11.0f
static float clamp(float v,float lo,float hi) { return fminf(hi,fmaxf(lo,v)); }
static float wrap(float v) { v=fmodf(v,TURN);return v<0?v+TURN:v; }
static float radians(float degrees) { return degrees*(PI/180.0f); }
static bool active(const position_control_t *s) {
    return s->state==SERVO_WAKE_CAL || s->state==SERVO_ALIGN || s->state==SERVO_SWEEP ||
        s->state==SERVO_RETURN || s->state==SERVO_WAKE_RUN || s->state==SERVO_RUNNING;
}
bool position_driving(const position_control_t *s) {
    return s->state==SERVO_ALIGN || s->state==SERVO_SWEEP || s->state==SERVO_RETURN || s->state==SERVO_RUNNING;
}
static void fail(position_control_t *s,servo_fault_t f,const control_input_t *in) {
    if (s->state==SERVO_FAULT) return; /* Preserve the first trip, not shutdown side effects. */
    s->trip_input=*in;s->trip_state=s->state;s->trip_encoder_reject=s->encoder_reject;s->trip_captured=true;
    s->state=SERVO_FAULT;s->fault=f;s->iq_target_a=s->vq_target_v=0;s->settled=false;
    s->id_integral=s->iq_integral=s->velocity_integral=0;
}
void position_init(position_control_t *s) { memset(s,0,sizeof(*s));s->direction=1; }
void position_trip(position_control_t *s,const control_input_t *in,servo_fault_t reason) { fail(s,reason,in); }
void position_stop(position_control_t *s) {
    if (s->state!=SERVO_FAULT) s->state=s->commutation_calibrated?SERVO_READY:SERVO_OFF;
    s->iq_target_a=s->vq_target_v=s->trajectory_rpm=0;s->settled=false;
    s->fast_count=s->previous_sequence=0;
    s->id_integral=s->iq_integral=s->velocity_integral=0;
}
static bool encoder_update(position_control_t *s,const control_input_t *in) {
    s->encoder_age_us=in->now_us-in->encoder_us;
    s->weak_magnet=(in->encoder_status&0x18u)!=0;
    bool good=in->encoder_transport && s->encoder_age_us<=CONTROL_ENCODER_MAX_AGE_US &&
        (in->encoder_status&0x20u) && (CONTROL_ALLOW_WEAK_MAGNET || !s->weak_magnet);
    s->encoder_reject=ENCODER_ACCEPTED;
    if (!good) {
        if (!in->encoder_transport) s->encoder_reject=ENCODER_TRANSPORT;
        else if (s->encoder_age_us>CONTROL_ENCODER_MAX_AGE_US) s->encoder_reject=ENCODER_STALE;
        else if (!(in->encoder_status&0x20u)) s->encoder_reject=ENCODER_MISSING_MAGNET;
        else s->encoder_reject=ENCODER_WEAK_FIELD;
        /* Loss of single-turn continuity invalidates the mechanical zero.
         * Recovery must not silently resume an old absolute position. */
        if (s->unwrap_started) s->zero_set=false;
        s->unwrap_started=s->velocity_started=false;
        return false;
    }
    if (!s->unwrap_started) {
        s->raw_turns=(float)in->encoder_raw/4096.0f;
        s->previous_raw=in->encoder_raw;s->previous_encoder_us=in->encoder_us;
        s->unwrap_started=true;s->velocity_rad_s=0;return true;
    }
    if (in->encoder_us==s->previous_encoder_us) return true;
    uint32_t dt=in->encoder_us-s->previous_encoder_us;
    int32_t delta=(int32_t)in->encoder_raw-s->previous_raw;
    if (delta>2048) delta-=4096;else if(delta< -2048) delta+=4096;
    if (dt>CONTROL_ENCODER_MAX_AGE_US || abs(delta)>256 || dt==0) {
        s->encoder_reject=abs(delta)>256?ENCODER_JUMP:ENCODER_SAMPLE_GAP;
        s->zero_set=false;s->unwrap_started=false;return false;
    }
    s->raw_turns+=(float)delta/4096.0f;
    float velocity=(float)s->direction*(float)delta*(TURN*1000000.0f/4096.0f)/(float)dt;
    s->velocity_rad_s+=0.15f*(velocity-s->velocity_rad_s);
    s->velocity_started=true;s->velocity_rpm=s->velocity_rad_s*(60.0f/TURN);
    s->previous_raw=in->encoder_raw;s->previous_encoder_us=in->encoder_us;
    if (s->zero_set) s->position_deg=(float)s->direction*(s->raw_turns-s->zero_turns)*360.0f;
    return true;
}
static void enter(position_control_t *s,servo_state_t state,uint32_t now) {
    s->state=state;s->stage_ms=now;s->cal_good_samples=0;
}
void position_tick(position_control_t *s,const control_input_t *in,servo_action_t action) {
    bool enc=encoder_update(s,in);
    s->current_age_us=in->now_us-in->current_us;
    if (in->hard_fault) { fail(s,in->hard_fault_reason==SERVO_OK?SERVO_DRIVER:in->hard_fault_reason,in);return; }
    if (action==SERVO_STOP) { position_stop(s);return; }
    if (s->state==SERVO_FAULT) return; /* Reset required: never automatically clear a trip. */
    if (active(s) && (!enc || !in->analog_valid)) { fail(s,SERVO_FEEDBACK,in);return; }
    if (active(s) && (!isfinite(in->bus_v) || in->bus_v<18 || in->bus_v>28)) { fail(s,SERVO_SUPPLY,in);return; }
    if (active(s) && (!in->temperature_valid || !isfinite(in->temperature_c) || in->temperature_c>60)) { fail(s,SERVO_TEMPERATURE,in);return; }
    if (position_driving(s) && (!in->driver_good || !in->current_valid || s->current_age_us>150u)) {
        fail(s,!in->driver_good?SERVO_DRIVER:SERVO_FEEDBACK,in);return;
    }
    if (action==SERVO_CALIBRATE && !active(s) && enc && CONTROL_MOTION_ENABLED) {
        s->commutation_calibrated=s->zero_set=false;s->cal_angle=0;
        enter(s,SERVO_WAKE_CAL,in->now_ms);
    } else if (action==SERVO_SET_ZERO && s->state==SERVO_READY && enc) {
        s->zero_turns=s->raw_turns;s->position_deg=0;s->zero_set=true;
    } else if (action==SERVO_ARM && s->state==SERVO_READY && s->zero_set && enc &&
               in->command_valid && CONTROL_MOTION_ENABLED) {
        s->trajectory_deg=s->position_deg;s->trajectory_rpm=0;
        s->velocity_integral=s->id_integral=s->iq_integral=0;
        s->following_ms=s->settled_ms=0;
        enter(s,SERVO_WAKE_RUN,in->now_ms);
    }
    uint32_t elapsed=in->now_ms-s->stage_ms;
    if (s->state==SERVO_WAKE_CAL || s->state==SERVO_WAKE_RUN) {
        if (elapsed>2000u) { fail(s,SERVO_ZERO_TIMEOUT,in);return; }
        if (elapsed>=350u && in->driver_good && in->zero_ready && in->current_valid && s->current_age_us<=150u) {
            enter(s,s->state==SERVO_WAKE_CAL?SERVO_ALIGN:SERVO_RUNNING,in->now_ms);
        }
    } else if (s->state==SERVO_ALIGN && elapsed>=2000u) {
        if (s->cal_good_samples<100u) { fail(s,SERVO_CALIBRATION,in);return; }
        s->cal_start_turns=s->raw_turns;enter(s,SERVO_SWEEP,in->now_ms);
    } else if (s->state==SERVO_SWEEP) {
        s->cal_angle=(PI/2)*clamp((float)elapsed/1500.0f,0,1);
        if (elapsed>=2500u) {
            float movement=(s->raw_turns-s->cal_start_turns)*TURN;
            float expected=(PI/2)/PP;
            if (fabsf(movement)<0.5f*expected || fabsf(movement)>1.5f*expected || s->cal_good_samples<100u) {
                fail(s,SERVO_CALIBRATION,in);return;
            }
            s->direction=movement>0?1:-1;enter(s,SERVO_RETURN,in->now_ms);
        }
    } else if (s->state==SERVO_RETURN) {
        s->cal_angle=(PI/2)*(1-clamp((float)elapsed/1500.0f,0,1));
        if (elapsed>=2500u) {
            if (fabsf(s->raw_turns-s->cal_start_turns)*TURN>0.3f/PP || s->cal_good_samples<100u) {
                fail(s,SERVO_CALIBRATION,in);return;
            }
            s->electrical_offset=wrap(-(float)s->direction*PP*(float)in->encoder_raw*(TURN/4096.0f));
            s->commutation_calibrated=true;position_stop(s);
        }
    }
    if (s->state!=SERVO_RUNNING) return;
    if (!s->zero_set || !enc) { fail(s,SERVO_FEEDBACK,in);return; }
    if (!in->command_valid || in->now_ms-in->command_ms>CONTROL_COMMAND_MAX_AGE_MS) {
        fail(s,SERVO_COMMAND_TIMEOUT,in);return;
    }
    if (!isfinite(in->target_deg) || in->target_deg<0 || in->target_deg>360) { fail(s,SERVO_TRAVEL,in);return; }
    if (s->position_deg< -0.5f || s->position_deg>360.5f || fabsf(s->velocity_rpm)>2*CONTROL_MAX_RPM) {
        fail(s,SERVO_TRAVEL,in);return;
    }
    s->requested_deg=in->target_deg;
    s->target_deg=clamp(in->target_deg,CONTROL_END_MARGIN_DEG,360-CONTROL_END_MARGIN_DEG);
    /* Linear error: deliberately no shortest-arc/wrap operation here. */
    float distance=s->target_deg-s->trajectory_deg;
    float a=CONTROL_ACCEL_RPM_S*6.0f;
    float desired=copysignf(fminf(CONTROL_MAX_RPM*6,sqrtf(2*a*fabsf(distance))),distance);
    float v=s->trajectory_rpm*6;
    v+=clamp(desired-v,-a*0.001f,a*0.001f);
    float step=v*0.001f;
    if (fabsf(step)>=fabsf(distance) && step*distance>=0) { s->trajectory_deg=s->target_deg;v=0; }
    else s->trajectory_deg=clamp(s->trajectory_deg+step,0,360);
    s->trajectory_rpm=v/6;
    float error=s->trajectory_deg-s->position_deg;
    if (fabsf(error)>30) { if (++s->following_ms>=200u) { fail(s,SERVO_FOLLOWING,in);return; } }
    else s->following_ms=0;
    float velocity_target=clamp(radians(v)+CONTROL_POSITION_KP*radians(error),
        -CONTROL_MAX_RPM*(TURN/60),CONTROL_MAX_RPM*(TURN/60));
    /* At the endpoints forbid an effort that would drive out of the turn. */
    float ev=velocity_target-s->velocity_rad_s;
    float kp=CONTROL_CURRENT_PI_ENABLED?CONTROL_VELOCITY_KP:CONTROL_VELOCITY_KP_V;
    float ki=CONTROL_CURRENT_PI_ENABLED?CONTROL_VELOCITY_KI:CONTROL_VELOCITY_KI_V;
    float maximum=CONTROL_CURRENT_PI_ENABLED?CONTROL_IQ_LIMIT_A:CONTROL_RUN_VOLTS;
    float candidate=s->velocity_integral+ki*ev*0.001f;
    float effort=kp*ev+candidate,applied=clamp(effort,-maximum,maximum);
    s->iq_target_a=CONTROL_CURRENT_PI_ENABLED?applied:0;
    s->vq_target_v=CONTROL_CURRENT_PI_ENABLED?0:applied;
    if (fabsf(effort)<=maximum || ev*effort<0) s->velocity_integral=candidate;
    if ((s->position_deg<=CONTROL_END_MARGIN_DEG && applied<0) ||
        (s->position_deg>=360-CONTROL_END_MARGIN_DEG && applied>0)) {
        s->iq_target_a=s->vq_target_v=0;s->velocity_integral=0;
    }
    if (fabsf(in->target_deg-s->position_deg)<=2.5f && fabsf(s->velocity_rpm)<0.5f) ++s->settled_ms;
    else s->settled_ms=0;
    s->settled=s->settled_ms>=250u;
}
void position_fast(position_control_t *s,const control_input_t *in,control_output_t *out) {
    memset(out,0,sizeof(*out));for (unsigned j=0;j<3;++j) out->duty[j]=0.5f;
    out->enable=active(s);out->pwm=position_driving(s);
    if (!out->pwm) { s->id_integral=s->iq_integral=0;return; }
    if (!in->current_valid || in->now_us-in->current_us>150u || !in->zero_ready ||
        !in->encoder_transport || in->now_us-in->encoder_us>CONTROL_ENCODER_MAX_AGE_US ||
        !(in->encoder_status&0x20u) || !in->driver_good || in->hard_fault ||
        (!CONTROL_ALLOW_WEAK_MAGNET && (in->encoder_status&0x18u)) ||
        !isfinite(in->bus_v) || in->bus_v<18 || in->bus_v>28) { fail(s,SERVO_FEEDBACK,in);goto off; }
    if (CONTROL_CURRENT_PI_ENABLED && in->current_sequence==s->previous_sequence) { fail(s,SERVO_TIMING,in);goto off; }
    uint32_t dt_us=in->current_us-s->previous_current_us;
    s->previous_current_us=in->current_us;s->previous_sequence=in->current_sequence;
    if (CONTROL_CURRENT_PI_ENABLED && s->fast_count && (dt_us<40u || dt_us>65u)) { fail(s,SERVO_TIMING,in);goto off; }
    ++s->fast_count;
    float sum=0;
    for (unsigned j=0;j<3;++j) {
        float current=in->current_a[j];
        if (!isfinite(current) || fabsf(current)>CONTROL_PHASE_TRIP_A) { fail(s,SERVO_CURRENT,in);goto off; }
        s->phase_a[j]=current;sum+=current;s->max_phase_a=fmaxf(s->max_phase_a,fabsf(current));
    }
    s->current_sum_a=sum;
    if (fabsf(sum)>0.30f) {
        if (s->sum_bad_count<8u) ++s->sum_bad_count;
        if (CONTROL_CURRENT_PI_ENABLED && s->sum_bad_count>=8u) { fail(s,SERVO_CURRENT_SUM,in);goto off; }
    }
    else s->sum_bad_count=0;
    float alpha=in->current_a[0]-sum/3;
    float beta=(in->current_a[1]-in->current_a[2])*0.5773502692f;
    float theta=s->state==SERVO_RUNNING?
        wrap((float)s->direction*PP*(float)in->encoder_raw*(TURN/4096.0f)+s->electrical_offset):s->cal_angle;
    float c=cosf(theta),sn=sinf(theta);
    s->id_a=alpha*c+beta*sn;s->iq_a=-alpha*sn+beta*c;
    float vd=0,vq=0,limit=fminf(CONTROL_VOLT_LIMIT,in->bus_v*0.15f);
    if (s->state!=SERVO_RUNNING) {
        uint32_t elapsed=in->now_ms-s->stage_ms;
        vd=CONTROL_CAL_VOLTS;
        if (s->state==SERVO_ALIGN) vd*=clamp((float)elapsed/500,0,1);
        /* Settled current must point along each applied d vector. Catches a
         * reversed current sign and B/C acquisition or motor phase mismatch. */
        if (elapsed>=1800u && (!CONTROL_CURRENT_PI_ENABLED ||
            (s->id_a>0.03f && fabsf(s->iq_a)<fmaxf(0.08f,s->id_a*0.5f)))) ++s->cal_good_samples;
    } else if (!CONTROL_CURRENT_PI_ENABLED) {
        /* Position/velocity control requests VOLTS. Encoder angle plus a
         * quadrature sine vector produces torque; measured current is used
         * for diagnostics and trips, not amplitude regulation in this stage. */
        vq=clamp(s->vq_target_v,-fminf(CONTROL_RUN_VOLTS,limit),fminf(CONTROL_RUN_VOLTS,limit));
        s->id_integral=s->iq_integral=0;
    } else {
        float ed=-s->id_a,eq=s->iq_target_a-s->iq_a;
        float di=s->id_integral+CONTROL_CURRENT_KI*ed*0.00005f;
        float qi=s->iq_integral+CONTROL_CURRENT_KI*eq*0.00005f;
        vd=CONTROL_CURRENT_KP*ed+di;vq=CONTROL_CURRENT_KP*eq+qi;
        float magnitude=hypotf(vd,vq),scale=magnitude>limit?limit/magnitude:1;
        /* Back-calculation for vector saturation, including measured bus limit. */
        s->id_integral=clamp(di+0.1f*(vd*scale-vd),-limit,limit);
        s->iq_integral=clamp(qi+0.1f*(vq*scale-vq),-limit,limit);
        vd*=scale;vq*=scale;
    }
    s->vd_v=vd;s->vq_v=vq;
    float va=vd*c-vq*sn,vb=vd*sn+vq*c;
    float phase[3]={va,-0.5f*va+0.8660254038f*vb,-0.5f*va-0.8660254038f*vb};
    for (unsigned j=0;j<3;++j) s->duty[j]=out->duty[j]=clamp(0.5f+phase[j]/in->bus_v,0.35f,0.65f);
    return;
off:
    out->enable=out->pwm=false;
}
