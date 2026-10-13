#include "motor_control.h"
#include "demo_protocol.h"
#include "demo_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define TAU 6.28318530718f
static motor_input_t healthy(uint32_t now,float angle) {
    motor_input_t in={0}; in.now_ms=now;
    in.raw_angle=(uint16_t)(motor_wrap(angle)*(4096.0f/TAU)) & 4095u;
    in.encoder_ms=in.health_ms=in.adc_ms=in.foreground_ms=in.command_ms=now;
    in.encoder_valid=in.magnet_good=in.adc_valid=in.driver_good=true;
    in.source=SOURCE_ANALOG; return in;
}
static void calibrated(motor_t *m,int direction,uint32_t *time) {
    motor_init(m); assert(motor_select_mode(m,MODE_CLOSED_LOOP)); motor_output_t out; float angle=1.0f;
    motor_input_t in=healthy(++*time,angle);
    motor_step(m,&in,ACTION_CALIBRATE,&out);
    for (unsigned i=0;i<7500 && m->state!=MOTOR_READY;++i) {
        uint32_t t=*time-m->stage_ms;
        if (m->state==MOTOR_CAL_SWEEP)
            angle=1.0f+direction*(TAU/4.0f/DEMO_POLE_PAIRS)*fminf(t/1500.0f,1);
        else if (m->state==MOTOR_CAL_RETURN)
            angle=1.0f+direction*(TAU/4.0f/DEMO_POLE_PAIRS)*(1-fminf(t/1500.0f,1));
        in=healthy(++*time,angle); motor_step(m,&in,ACTION_NONE,&out);
        assert(m->state!=MOTOR_FAULT);
    }
    assert(m->state==MOTOR_READY && m->calibrated && !m->zeroed);
    assert(m->direction==direction && !out.enable && !out.pwm);
    in=healthy(++*time,angle); motor_step(m,&in,ACTION_ZERO,&out);
    assert(m->zeroed && fabsf(m->position)<0.001f);
}
static void test_calibration_and_control(void) {
    for (int dir=-1;dir<=1;dir+=2) {
        motor_t m; motor_output_t out; uint32_t time=0; calibrated(&m,dir,&time);
        motor_input_t in=healthy(++time,1); in.target_rad=TAU;
        motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_ARM_WAKE && !out.pwm);
        for (unsigned i=0;i<20000;++i) {
            float old_velocity=m.reference_velocity;
            in=healthy(++time,1+dir*m.reference); in.target_rad=TAU; motor_step(&m,&in,ACTION_NONE,&out);
            assert(m.state!=MOTOR_FAULT);
            assert(m.reference>=0 && m.reference<=TAU);
            assert(fabsf(m.reference_velocity)<=DEMO_MAX_SPEED+1e-5f);
            assert(fabsf(m.reference_velocity-old_velocity)<=DEMO_ACCEL*0.001f+1e-5f);
            assert(fabsf(m.effort)<=DEMO_MAX_Q_VOLTS+1e-5f);
            for (unsigned j=0;j<3;++j) assert(out.duty[j]>=0.05f && out.duty[j]<=0.95f);
        }
        assert(m.reference>6.0f); /* Does not use shortest modulo error to stay at zero. */
        in=healthy(++time,1+dir*m.reference); in.target_rad=TAU; motor_step(&m,&in,ACTION_DISABLE,&out);
        assert(m.state==MOTOR_READY && !out.enable && !out.pwm && m.integral==0);
    }
}
static void test_faults(void) {
    for (unsigned kind=0;kind<10;++kind) {
        motor_t m; motor_output_t out; uint32_t t=0; calibrated(&m,1,&t);
        motor_input_t in=healthy(++t,1); motor_step(&m,&in,ACTION_ARM,&out);
        for (unsigned i=0;i<12;++i) { in=healthy(++t,1); motor_step(&m,&in,ACTION_NONE,&out); }
        assert(m.state==MOTOR_RUNNING);
        in=healthy(++t,1);
        if (kind==0) in.encoder_valid=false;
        if (kind==1) in.driver_good=false;
        if (kind==2) in.adc_valid=false;
        if (kind==3) in.foreground_ms=t-101;
        if (kind==4) { in.source=SOURCE_ETHERNET; in.command_ms=t-501; }
        if (kind==5) in.target_rad=NAN;
        if (kind==6) { in.hard_fault=true; in.external_fault=FAULT_DRIVER; }
        if (kind==7) in.encoder_ms=t-DEMO_ENCODER_MAX_AGE_MS-1;
        if (kind==8) in.health_ms=t-DEMO_HEALTH_MAX_AGE_MS-1;
        if (kind==9) in.adc_ms=t-DEMO_ADC_MAX_AGE_MS-1;
        motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state==MOTOR_FAULT && !out.enable && !out.pwm);
        in=healthy(++t,1); motor_step(&m,&in,ACTION_ARM,&out);
        assert(m.state==MOTOR_FAULT && !out.enable);
        motor_step(&m,&in,ACTION_CLEAR_FAULT,&out);
        assert(m.state==MOTOR_DISABLED && !m.calibrated && !m.zeroed);
    }
    motor_t m; motor_output_t out; motor_init(&m); assert(motor_select_mode(&m,MODE_CLOSED_LOOP));
    motor_input_t in=healthy(1,1); motor_step(&m,&in,ACTION_ARM,&out);
    assert(m.state==MOTOR_DISABLED && !out.enable);
    motor_step(&m,&in,ACTION_CALIBRATE,&out);
    for (uint32_t t=2;t<5015;++t) { in=healthy(t,1); motor_step(&m,&in,ACTION_NONE,&out); }
    assert(m.state==MOTOR_FAULT && m.fault==FAULT_CALIBRATION && !out.pwm);
    uint32_t t=0; calibrated(&m,1,&t); in=healthy(++t,1);
    motor_step(&m,&in,ACTION_ARM,&out);
    for (unsigned i=0;i<5000 && m.state!=MOTOR_FAULT;++i) {
        in=healthy(++t,1); in.target_rad=TAU; motor_step(&m,&in,ACTION_NONE,&out);
    }
    assert(m.state==MOTOR_FAULT && m.fault==FAULT_TRACKING && !out.pwm);
}
static void test_reversal_and_disable_priority(void) {
    motor_t m; motor_output_t out; uint32_t t=0; calibrated(&m,1,&t);
    motor_input_t in=healthy(++t,1); motor_step(&m,&in,ACTION_ARM,&out);
    for (unsigned i=0;i<16000;++i) {
        float old_velocity=m.reference_velocity;
        in=healthy(++t,1+m.reference); in.target_rad=i<5000?TAU:0;
        motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state!=MOTOR_FAULT);
        assert(fabsf(m.reference_velocity-old_velocity)<=DEMO_ACCEL*0.001f+1e-5f);
        assert(m.reference>=0 && m.reference<=TAU);
    }
    assert(m.reference<0.001f);
    in=healthy(++t,1+m.reference); in.stop=true;
    motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_READY && !out.enable);
}
static void test_unwrap_and_clock_wrap(void) {
    motor_t m; motor_output_t out; motor_init(&m); assert(motor_select_mode(&m,MODE_CLOSED_LOOP));
    motor_input_t in=healthy(UINT32_MAX-2,TAU-0.02f); motor_step(&m,&in,ACTION_NONE,&out);
    float start=m.raw_unwrapped;
    in=healthy(0,0.02f); motor_step(&m,&in,ACTION_NONE,&out);
    assert(m.raw_unwrapped>start && fabsf(m.raw_unwrapped-start-0.04f)<0.003f);
    in=healthy(1,TAU-0.02f); motor_step(&m,&in,ACTION_NONE,&out);
    assert(fabsf(m.raw_unwrapped-start)<0.003f);
    float duties[3]; motor_modulate(0,0,0,duties);
    for (unsigned i=0;i<3;++i) assert(duties[i]==0.5f);
}
static void test_protocol(void) {
    net_command_t c;
    const char *valid="G1 1 4294967295 POSITION 360000";
    assert(protocol_parse(valid,strlen(valid),&c) && c.millidegrees==360000u);
    const char *invalid[]={"G1 0 1 HEARTBEAT","G1 1 1 POSITION 360001","G1 1 1 POSITION -1",
        "G1 1 1 POSITION 42949672960","G1 1 1 STATUS extra","G1  1 1 STATUS",
        "G1 1 1 STATUS ","G1 1 1 STATUS\n","G1 1 1 ARM","G1 1 1 POSITION 1 extra"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        assert(!protocol_parse(invalid[i],strlen(invalid[i]),&c));
    char embedded[]="G1 1 1 STATUS\0junk"; assert(!protocol_parse(embedded,sizeof(embedded)-1,&c));
    assert(protocol_newer(0,UINT32_MAX)); assert(!protocol_newer(1,1));
    assert(!protocol_newer(UINT32_MAX,0)); assert(!protocol_newer(0x80000000u,0));
}
int main(void) {
    test_calibration_and_control(); test_faults(); test_reversal_and_disable_priority();
    test_unwrap_and_clock_wrap(); test_protocol();
    puts("PASS: calibration both directions, bounded full-turn trajectory, effort limits, fault inhibition,\n"
         "stale commands/sensors, restart interlock, angle/time wrap, and strict UDP parsing.");
    return 0;
}
