/* Homing against a simulated pan hard stop, using the actual servo controller.
 * Plant: shaft speed proportional to applied quadrature voltage, clamped at the
 * stop. Built with PCB_TEST_AXIS=1 (pan) and voltage mode. */
#include "position_control.h"
#include "control_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEG_PER_S_PER_V 40.0f  /* 30 deg/s homing needs about 0.75 V, well under the 2 V cap */
static float shaft_deg,stop_deg;  /* Mechanical coordinate: pan positive is CCW, stop below. */
static uint32_t t_ms,lease_ms;

static uint16_t raw_of(float deg) { /* Pan encoder sign -1, arbitrary mounting offset. */
    return (uint16_t)(((int32_t)lroundf(1500.0f-deg*4096.0f/360.0f))&4095);
}
static control_input_t input(void) {
    control_input_t i={0};i.now_ms=t_ms;i.now_us=i.encoder_us=i.current_us=t_ms*1000u;
    i.current_sequence=t_ms*20u+1;i.encoder_raw=raw_of(shaft_deg);i.encoder_status=0x33;
    i.encoder_transport=i.driver_good=i.zero_ready=i.current_valid=i.analog_valid=i.temperature_valid=true;
    i.command_valid=true;i.command_ms=lease_ms;i.bus_v=24;i.temperature_c=25;return i;
}
static void step(position_control_t *s,servo_action_t action) {
    ++t_ms;control_input_t in=input();position_tick(s,&in,action);
    if (s->state==SERVO_HOMING || s->state==SERVO_RUNNING) {
        control_output_t out;position_fast(s,&in,&out);assert(out.pwm || s->state==SERVO_FAULT);
        shaft_deg=fmaxf(stop_deg,shaft_deg+DEG_PER_S_PER_V*s->vq_target_v*0.001f);
    }
}
static position_control_t calibrated_pan(float start_deg,float stop) {
    shaft_deg=start_deg;stop_deg=stop;t_ms=lease_ms=1000;
    position_control_t s;position_init_axis(&s,1);
    s.state=SERVO_READY;s.commutation_calibrated=true;s.direction=1;s.electrical_offset=0.5f;
    step(&s,SERVO_NO_ACTION);assert(s.unwrap_started && !s.zero_set);
    return s;
}
static unsigned run_home(position_control_t *s,unsigned limit_ms,bool keep_lease) {
    step(s,SERVO_HOME);assert(s->state==SERVO_WAKE_RUN && s->homing);
    unsigned n=0;
    while ((s->state==SERVO_WAKE_RUN || s->state==SERVO_HOMING) && n<limit_ms) {
        if (keep_lease) lease_ms=t_ms;
        step(s,SERVO_NO_ACTION);++n;
    }
    return n;
}

static void test_homes_to_stop(void) {
    position_control_t s=calibrated_pan(150.0f,-0.0f);
    unsigned ms=run_home(&s,CONTROL_HOME_TIMEOUT_MS+2000u,true);
    assert(s.state==SERVO_READY && s.zero_set && !s.homing && s.fault==SERVO_OK);
    assert(fabsf(shaft_deg-stop_deg)<0.01f);              /* It really reached the stop... */
    assert(fabsf(s.position_deg)<0.2f);                    /* ...and that is now 0 degrees. */
    assert(ms>4500u && ms<8000u);                          /* ~150 deg at 30 deg/s plus wake/stall */
    /* The servo coordinate now matches the shaft: move away by hand and read it back. */
    while (shaft_deg<45.0f) { shaft_deg+=0.05f;step(&s,SERVO_NO_ACTION); } /* 50 deg/s by hand */
    assert(s.zero_set && fabsf(s.position_deg-45.0f)<0.2f);
    /* A later ARM runs normal position control, not homing again. */
    lease_ms=t_ms;step(&s,SERVO_ARM);assert(s.state==SERVO_WAKE_RUN && !s.homing);
    for (unsigned k=0;k<400;++k) { lease_ms=t_ms;step(&s,SERVO_NO_ACTION); }
    assert(s.state==SERVO_RUNNING);
    puts("PASS pan homing: wake, slow move, stall at the hard stop defines 0 degrees");
}
static void test_already_at_stop(void) {
    position_control_t s=calibrated_pan(0.0f,0.0f);
    unsigned ms=run_home(&s,5000u,true);
    assert(s.state==SERVO_READY && s.zero_set && fabsf(s.position_deg)<0.2f);
    assert(ms<350u+CONTROL_HOME_SETTLE_MS+CONTROL_HOME_STALL_MS+300u);
}
static void test_no_stop_times_out(void) {
    position_control_t s=calibrated_pan(150.0f,-1e9f);
    run_home(&s,CONTROL_HOME_TIMEOUT_MS+2000u,true);
    assert(s.state==SERVO_FAULT && s.fault==SERVO_CALIBRATION && !s.zero_set);
    assert(150.0f-shaft_deg<(CONTROL_HOME_TIMEOUT_MS/1000.0f+1)*CONTROL_HOME_RPM*6*1.2f); /* Bounded travel. */
    puts("PASS homing without a stop faults at the timeout with bounded travel");
}
static void test_lease_loss(void) {
    position_control_t s=calibrated_pan(150.0f,0.0f);
    run_home(&s,3000u,false);                               /* Laptop stops sending heartbeats. */
    assert(s.state==SERVO_FAULT && s.fault==SERVO_COMMAND_TIMEOUT && !s.zero_set);
}
static void test_rejections(void) {
    position_control_t s=calibrated_pan(100.0f,0.0f);
    s.commutation_calibrated=false;step(&s,SERVO_HOME);assert(s.state==SERVO_READY && !s.homing);
    s=calibrated_pan(100.0f,0.0f);
    ++t_ms;control_input_t in=input();in.command_valid=false;position_tick(&s,&in,SERVO_HOME);
    assert(s.state==SERVO_READY && !s.homing);
    position_control_t tilt;position_init_axis(&tilt,0);   /* No tilt stop configured. */
    assert(!position_home_supported(&tilt) && position_home_supported(&s));
    tilt.state=SERVO_READY;tilt.commutation_calibrated=true;tilt.unwrap_started=true;
    ++t_ms;in=input();position_tick(&tilt,&in,SERVO_HOME);assert(tilt.state==SERVO_READY);
    /* Homing pushes at its stop even though position_deg is still 0: no endpoint cut-off. */
    s=calibrated_pan(100.0f,0.0f);step(&s,SERVO_HOME);
    for (unsigned k=0;k<400 && s.state!=SERVO_HOMING;++k) { lease_ms=t_ms;step(&s,SERVO_NO_ACTION); }
    for (unsigned k=0;k<50;++k) { lease_ms=t_ms;step(&s,SERVO_NO_ACTION); }
    assert(s.state==SERVO_HOMING && s.vq_target_v<0 && fabsf(s.vq_v)>0);
    /* STOP during homing returns to READY without a zero. */
    step(&s,SERVO_STOP);assert(s.state==SERVO_READY && !s.homing && !s.zero_set);
}
int main(void) {
    assert(PCB_TEST_AXIS==1 && !CONTROL_CURRENT_PI_ENABLED && CONTROL_HOME_TIMEOUT_MS>11000u);
    test_homes_to_stop();test_already_at_stop();test_no_stop_times_out();test_lease_loss();test_rejections();
    puts("PASS homing lease loss, rejections, stop and endpoint bypass");
    return 0;
}
