#include "position_control.h"
#include "commission.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static position_control_t running(float deg,int direction) {
    position_control_t s;position_init(&s);
    s.state=SERVO_RUNNING;s.commutation_calibrated=s.zero_set=s.unwrap_started=true;
    s.direction=(int8_t)direction;s.zero_turns=3600.0f/4096;
    s.raw_turns=s.zero_turns-deg/360;s.position_deg=s.trajectory_deg=deg;
    s.previous_raw=(uint16_t)(s.raw_turns*4096);s.previous_encoder_us=500;
    return s;
}
static control_input_t sample(const position_control_t *s) {
    return (control_input_t){.now_us=1000,.now_ms=1,.encoder_us=1000,.current_us=1000,
        .current_sequence=1,.encoder_raw=s->previous_raw,.encoder_status=0x33,
        .encoder_transport=true,.driver_good=true,.zero_ready=true,.current_valid=true,
        .analog_valid=true,.temperature_valid=true,.command_valid=true,.command_ms=1,
        .bus_v=24,.temperature_c=25,.target_deg=180};
}
int main(void) {
    for(int direction=-1;direction<=1;direction+=2) {
        position_control_t s=running(0,direction);control_input_t in=sample(&s);
        in.encoder_raw-=10;in.target_deg=10;
        position_tick(&s,&in,SERVO_NO_ACTION);
        assert(s.state==SERVO_RUNNING && s.position_deg>0.8f && s.velocity_rpm>0);
        assert(position_effort_sign(&s)==-direction);
        s=running(180,direction);in=sample(&s);control_output_t out;
        s.vq_target_v=0.5f;s.iq_target_a=0.2f;
        position_fast(&s,&in,&out);
        assert(out.pwm && s.vq_v*position_effort_sign(&s)>0);
        s=running(337,direction);in=sample(&s);in.experiment_mode=1;in.experiment_value=1;
        in.target_deg=337;position_tick(&s,&in,SERVO_NO_ACTION);
        assert(s.state==SERVO_RUNNING && s.target_deg<=337);
        s.vq_target_v=1;s.iq_target_a=0.2f;position_fast(&s,&in,&out);
        assert(fabsf(s.vq_v)<0.00001f);
        s=running(0,direction);in=sample(&s);
        s.vq_target_v=-1;s.iq_target_a=-0.2f;position_fast(&s,&in,&out);
        assert(fabsf(s.vq_v)<0.00001f);
        s=running(180,direction);in=sample(&s);in.target_deg=338;
        position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_TRAVEL);
        s=running(180,direction);in=sample(&s);s.vq_v=0.5f*position_effort_sign(&s);
        commission_init();g_commission.running=true;commission_capture(&in,&s);
        unsigned char bytes[728];commission_sample_t row;
        assert(commission_read(0,bytes,sizeof(bytes))==24+sizeof(row));
        memcpy(&row,bytes+24,sizeof(row));assert(fabsf(row.applied_v-0.5f)<0.00001f);
    }
    puts("PASS pan CCW coordinates, both phase orders, endpoint effort, travel rejection and trace polarity");
}
