#include "position_control.h"
#include "control_config.h"
#include "demo_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846f
static control_input_t sample(uint32_t us) {
    control_input_t i={0};i.now_us=i.encoder_us=i.current_us=us;i.now_ms=us/1000;
    i.current_sequence=us/50+1;i.encoder_transport=true;i.encoder_status=0x33;
    i.driver_good=i.zero_ready=i.current_valid=i.analog_valid=i.temperature_valid=i.command_valid=true;
    i.bus_v=24;i.temperature_c=25;i.command_ms=i.now_ms;return i;
}
static position_control_t running(float deg) {
    position_control_t s;position_init(&s);s.state=SERVO_RUNNING;
    s.commutation_calibrated=s.zero_set=s.unwrap_started=true;
    s.raw_turns=deg/360;s.position_deg=s.trajectory_deg=deg;
    s.previous_raw=(uint16_t)(deg*4096/360);return s;
}
static void trajectory(void) {
    position_control_t s=running(350);control_input_t in=sample(1000);
    in.encoder_raw=s.previous_raw;in.target_deg=10;
    for (unsigned k=0;k<150;++k) {
        in.now_us=in.encoder_us=in.current_us=(k+1)*1000;in.now_ms=in.command_ms=k+1;
        position_tick(&s,&in,SERVO_NO_ACTION);
        assert(s.state==SERVO_RUNNING && s.trajectory_deg<=350 && s.trajectory_deg>10);
        assert(s.trajectory_rpm<=0 && fabsf(s.trajectory_rpm)<=CONTROL_MAX_RPM);
    }
    assert(s.trajectory_deg<349); /* Long negative route, never across 360. */
    s=running(1);in=sample(1000);in.encoder_raw=s.previous_raw;in.target_deg=0;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.target_deg==2 && s.iq_target_a>=0);
    assert(s.trajectory_deg>=1 && s.trajectory_deg<2);
    assert(s.trajectory_deg-1 <= CONTROL_ACCEL_RPM_S*6.0f*0.001f*0.001f+0.00001f);
    s=running(359);in=sample(1000);in.encoder_raw=s.previous_raw;in.target_deg=360;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.target_deg==358 && s.iq_target_a<=0);
    puts("PASS bounded linear trajectory, acceleration and endpoint effort");
}
static void faults(void) {
    control_input_t in=sample(1000);position_control_t s=running(180);in.encoder_raw=s.previous_raw;
    in.encoder_transport=false;position_tick(&s,&in,SERVO_NO_ACTION);
    assert(s.state==SERVO_FAULT && s.fault==SERVO_FEEDBACK && !s.zero_set);
    position_tick(&s,&in,SERVO_ARM);assert(s.state==SERVO_FAULT);
    s=running(180);in=sample(1000000);in.encoder_raw=s.previous_raw;in.command_ms=0;
    s.previous_encoder_us=in.encoder_us;position_tick(&s,&in,SERVO_NO_ACTION);
    assert(s.fault==SERVO_COMMAND_TIMEOUT);
    s=running(0);in=sample(1000);position_tick(&s,&in,SERVO_STOP);assert(s.state==SERVO_READY);
    s=running(180);in=sample(1000);in.encoder_raw=s.previous_raw;in.current_us=0;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_FEEDBACK);
    s=running(180);in=sample(1000);in.encoder_raw=s.previous_raw;in.temperature_c=61;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_TEMPERATURE);
    s=running(180);in=sample(1000);in.encoder_raw=s.previous_raw;in.bus_v=29;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_SUPPLY);
    s=running(180);in=sample(1000);in.encoder_raw=s.previous_raw;in.target_deg=NAN;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_TRAVEL);
    s=running(180);in=sample(1000);in.hard_fault=true;in.hard_fault_reason=SERVO_TIMING;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_TIMING);
    in.hard_fault_reason=SERVO_DRIVER;position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_TIMING);
    position_init(&s);in.hard_fault_reason=SERVO_FOREGROUND;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_FOREGROUND);
    position_init(&s);s.state=SERVO_WAKE_CAL;in=sample(1000);in.analog_valid=false;
    in.adc_failed_mask=1;in.adc_errors[0]=1;
    position_tick(&s,&in,SERVO_NO_ACTION);
    assert(s.fault==SERVO_FEEDBACK && s.trip_captured && s.trip_state==SERVO_WAKE_CAL);
    assert(s.trip_encoder_reject==ENCODER_ACCEPTED && !s.trip_input.analog_valid && s.trip_input.adc_failed_mask==1);
    in=sample(2000);position_tick(&s,&in,SERVO_NO_ACTION);
    assert(!s.trip_input.analog_valid && s.trip_input.adc_errors[0]==1); /* Evidence survives recovery. */
    position_init(&s);s.state=SERVO_WAKE_CAL;in=sample(1000);in.encoder_status=0;
    position_tick(&s,&in,SERVO_NO_ACTION);
    assert(s.fault==SERVO_FEEDBACK && s.trip_encoder_reject==ENCODER_MISSING_MAGNET);
    position_init(&s);in=sample(1000); /* 0x33 includes MD plus weak-field warning. */
    position_tick(&s,&in,SERVO_CALIBRATE);
    assert(s.weak_magnet && s.encoder_reject==ENCODER_ACCEPTED && s.state!=SERVO_FAULT);
    puts("PASS feedback loss, stale current, lease expiry, stop, thermal and supply trips");
}
static void current(void) {
    position_control_t s=running(0);s.iq_target_a=0.4f;s.vq_target_v=0.8f;control_output_t out;
    control_input_t in=sample(1000);position_fast(&s,&in,&out);
    assert(out.pwm && s.vq_v>0 && fabsf(s.vd_v)<0.001f);
    in=sample(1050);in.current_a[0]=0;in.current_a[1]=0.8f*0.8660254f;in.current_a[2]=-in.current_a[1];
    position_fast(&s,&in,&out);
    if (CONTROL_CURRENT_PI_ENABLED) assert(s.vq_v<0); /* Negative current feedback. */
    else assert(fabsf(s.vq_v-0.8f)<0.001f); /* Current does not regulate sine-voltage amplitude. */
    for (unsigned k=0;k<2000;++k) {
        in=sample(1100+k*50);position_fast(&s,&in,&out);
        assert(out.pwm && hypotf(s.vd_v,s.vq_v)<=CONTROL_VOLT_LIMIT+0.001f);
        for(unsigned j=0;j<3;++j) assert(out.duty[j]>=0.5f-position_duty_span() && out.duty[j]<=0.5f+position_duty_span());
    }
    in=sample(101100);in.current_a[0]=1.6f;position_fast(&s,&in,&out);
    assert(s.fault==SERVO_CURRENT && !out.pwm && !out.enable);
    s=running(0);
    for(unsigned k=0;k<8;++k) {
        in=sample(1000+k*50);in.current_a[0]=0.4f;position_fast(&s,&in,&out);
    }
    if (CONTROL_CURRENT_PI_ENABLED) assert(s.fault==SERVO_CURRENT_SUM && !out.pwm);
    else assert(s.fault==SERVO_OK && out.pwm && s.sum_bad_count==8);
    s=running(0);in=sample(1000);position_fast(&s,&in,&out);position_fast(&s,&in,&out);
    if (CONTROL_CURRENT_PI_ENABLED) assert(s.fault==SERVO_TIMING && !out.pwm);
    else assert(s.fault==SERVO_OK && out.pwm);
    s=running(0);in=sample(1000);position_fast(&s,&in,&out);
    in=sample(1080);position_fast(&s,&in,&out);
    if (CONTROL_CURRENT_PI_ENABLED) assert(s.fault==SERVO_TIMING && !out.pwm);
    else assert(s.fault==SERVO_OK && out.pwm);
    s=running(0);in=sample(1000);in.current_us=800;position_fast(&s,&in,&out);
    assert(s.fault==SERVO_FEEDBACK && !out.pwm); /* Stale monitoring still trips. */
    s=running(0);in=sample(1000);in.current_a[1]=NAN;position_fast(&s,&in,&out);
    assert(s.fault==SERVO_CURRENT && !out.pwm);
    puts(CONTROL_CURRENT_PI_ENABLED?"PASS d/q feedback sign, anti-windup and trips":
        "PASS sinusoidal voltage command independent of current PI, duty bounds and current trips");
}
static void calibration(int direction) {
    position_control_t s;position_init(&s);control_output_t out;
    control_input_t in=sample(1000);in.encoder_raw=2048;
    position_tick(&s,&in,SERVO_CALIBRATE);
    if (!CONTROL_MOTION_ENABLED) { assert(s.state==SERVO_OFF);return; }
    assert(s.state==SERVO_WAKE_CAL);
    for (uint32_t us=1050;us<8500000;us+=50) {
        in=sample(us);
        /* Ideal fixture follows the calibration vector. This validates the
         * state machine, not real motor parameters or current-channel wiring. */
        float raw=2048+(float)direction*s.cal_angle*(4096/(2*PI*11));
        in.encoder_raw=(uint16_t)lroundf(raw);
        float alpha=0.2f*cosf(s.cal_angle),beta=0.2f*sinf(s.cal_angle);
        in.current_a[0]=alpha;in.current_a[1]=-0.5f*alpha+0.8660254f*beta;
        in.current_a[2]=-0.5f*alpha-0.8660254f*beta;
        if (us%1000==0) position_tick(&s,&in,SERVO_NO_ACTION);
        position_fast(&s,&in,&out);
        assert(s.state!=SERVO_FAULT);
        if (s.state==SERVO_READY) break;
    }
    assert(s.commutation_calibrated && !s.zero_set && s.direction==direction);
    in=sample(8600000);in.encoder_raw=2048;
    /* Continue supplying fresh samples while manually moving to the zero. */
    s.previous_encoder_us=in.encoder_us;s.previous_raw=in.encoder_raw;
    position_tick(&s,&in,SERVO_SET_ZERO);assert(s.zero_set && fabsf(s.position_deg)<0.01f);
    in=sample(8601000);in.encoder_raw=2048;position_tick(&s,&in,SERVO_ARM);assert(s.state==SERVO_WAKE_RUN);
    puts(direction>0?"PASS positive calibration, separate mechanical zero and physical arm":"PASS negative encoder calibration");
}
static void protocol(void) {
    net_command_t c;
    const char *ok="G1 1 2 POSITION 360000";assert(protocol_parse(ok,strlen(ok),&c));
    const char *bad[]={"G1 0 2 POSITION 3","G1 1 2 POSITION 360001","G1 1 2 POSITION -1",
        "G1 1 2 POSITION nan","G1 1 2 POSITION 4294967296","G1 1 2 STATUS x","G1  1 2 STATUS"};
    for(unsigned j=0;j<sizeof(bad)/sizeof(bad[0]);++j) assert(!protocol_parse(bad[j],strlen(bad[j]),&c));
    assert(protocol_newer(0,0xffffffffu) && !protocol_newer(4,4) && !protocol_newer(3,4));
    puts("PASS bounded strict protocol, overflow and sequence wrap/replay checks");
}
int main(void) {
    trajectory();faults();current();calibration(1);calibration(-1);protocol();return 0;
}
