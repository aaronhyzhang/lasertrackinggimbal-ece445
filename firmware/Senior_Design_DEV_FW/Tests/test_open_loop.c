#include "motor_control.h"
#include "demo_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define TAU 6.28318530718f
static motor_input_t input(uint32_t now,float voltage) {
    motor_input_t in={0}; in.now_ms=now; in.adc_ms=in.foreground_ms=now;
    in.adc_valid=in.driver_good=true; in.source=SOURCE_ANALOG; in.analog_volts=voltage;
    /* Encoder deliberately absent. */
    return in;
}
static void select_open(motor_t *m) {
    motor_init(m); assert(motor_select_mode(m,MODE_OPEN_LOOP));
    assert(m->state==MOTOR_DISABLED && !m->calibrated && !m->zeroed);
}
static void start(motor_t *m,uint32_t *now,motor_output_t *out) {
    select_open(m); motor_input_t in=input(++*now,0);
    motor_step(m,&in,ACTION_ARM,out); assert(m->state==MOTOR_OPEN_IDLE && !out->enable && !out->pwm);
    for (unsigned i=0;i<DEMO_OPEN_ALIGN_MS+20;++i) {
        in=input(++*now,3); motor_step(m,&in,ACTION_NONE,out);
    }
    assert(m->state==MOTOR_OPEN_RUNNING && out->pwm);
}
static void test_mapping(void) {
    assert(motor_open_speed_from_voltage(0)==0);
    assert(motor_open_speed_from_voltage(DEMO_OPEN_ZERO_VOLTS)==0);
    assert(fabsf(motor_open_speed_from_voltage(1.5f)-DEMO_OPEN_MAX_RPM*TAU/120)<1e-6f);
    assert(fabsf(motor_open_speed_from_voltage(3)-DEMO_OPEN_MAX_RPM*TAU/60)<1e-6f);
    assert(motor_open_speed_from_voltage(3.1f)==motor_open_speed_from_voltage(3));
    assert(isnan(motor_open_speed_from_voltage(NAN)));
    assert(isnan(motor_open_speed_from_voltage(-0.01f)));
    assert(isnan(motor_open_speed_from_voltage(3.2f)));
}
static void test_spin_and_zero(void) {
    motor_t m; motor_output_t out; uint32_t now=0; start(&m,&now,&out);
    float dv=DEMO_OPEN_ACCEL_RPM_S*TAU/60*0.001f;
    unsigned wraps=0;
    for (unsigned i=0;i<12000;++i) {
        float old_speed=m.open_speed,old_angle=m.open_angle;
        motor_input_t in=input(++now,3); motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state==MOTOR_OPEN_RUNNING && out.enable && out.pwm);
        assert(m.open_speed>=0 && m.open_speed<=DEMO_OPEN_MAX_RPM*TAU/60+1e-5f);
        assert(fabsf(m.open_speed-old_speed)<=dv+1e-5f);
        float increment=m.open_angle-old_angle; if (increment<0) { increment+=TAU; ++wraps; }
        assert(fabsf(increment-DEMO_POLE_PAIRS*m.open_speed*0.001f)<2e-6f);
        assert(m.open_angle>=0 && m.open_angle<TAU);
        assert(m.open_volts>=0 && m.open_volts<=DEMO_OPEN_VOLTS);
        assert(m.integral==0 && !m.calibrated && !m.zeroed);
        for (unsigned phase=0;phase<3;++phase) assert(out.duty[phase]>=0.05f && out.duty[phase]<=0.95f);
    }
    assert(wraps>10); /* Continuous spin is not subject to the closed-loop one-turn limit. */
    assert(fabsf(m.open_speed-DEMO_OPEN_MAX_RPM*TAU/60)<1e-5f);
    const unsigned brake_ticks=(unsigned)ceilf(DEMO_OPEN_MAX_RPM/DEMO_OPEN_ACCEL_RPM_S*1000.0f)+50u;
    for (unsigned i=0;i<brake_ticks && m.state!=MOTOR_OPEN_IDLE;++i) {
        float old=m.open_speed; motor_input_t in=input(++now,0);
        motor_step(&m,&in,ACTION_NONE,&out); assert(fabsf(m.open_speed-old)<=dv+1e-5f);
    }
    assert(m.state==MOTOR_OPEN_IDLE && m.open_speed==0 && m.open_volts==0 && !out.pwm && !out.enable);
    assert(!motor_select_mode(&m,MODE_CLOSED_LOOP)); /* Armed even though coasting. */
    motor_input_t in=input(++now,0); motor_step(&m,&in,ACTION_DISABLE,&out);
    assert(m.state==MOTOR_DISABLED && motor_select_mode(&m,MODE_CLOSED_LOOP));
    motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_DISABLED && !out.enable);
}
static void test_feedback_independence(void) {
    motor_t a,b; motor_output_t oa,ob; uint32_t now=0; select_open(&a); select_open(&b);
    for (unsigned i=0;i<8000;++i) {
        motor_input_t ia=input(++now,i?1.5f:0),ib=ia;
        ib.encoder_valid=ib.magnet_good=true; ib.raw_angle=(uint16_t)(i*173u)&4095u;
        ib.encoder_ms=now; ib.health_ms=now; /* Impossible/noisy feedback must not change drive. */
        motor_step(&a,&ia,i?ACTION_NONE:ACTION_ARM,&oa);
        motor_step(&b,&ib,i?ACTION_NONE:ACTION_ARM,&ob);
        assert(a.state==b.state && a.fault==b.fault && oa.enable==ob.enable && oa.pwm==ob.pwm);
        for (unsigned phase=0;phase<3;++phase) assert(oa.duty[phase]==ob.duty[phase]);
    }
}
static void test_faults_and_interlocks(void) {
    for (unsigned kind=0;kind<6;++kind) {
        motor_t m; motor_output_t out; uint32_t now=0; start(&m,&now,&out);
        motor_input_t in=input(++now,3);
        if (kind==0) in.adc_valid=false;
        if (kind==1) in.adc_ms=now-DEMO_ADC_MAX_AGE_MS-1;
        if (kind==2) in.foreground_ms=now-DEMO_FOREGROUND_MAX_AGE_MS-1;
        if (kind==3) in.driver_good=false;
        if (kind==4) { in.hard_fault=true; in.external_fault=FAULT_DRIVER; }
        if (kind==5) in.analog_volts=3.2f;
        motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state==MOTOR_FAULT && !out.pwm && !out.enable && m.open_speed==0 && m.open_volts==0);
        assert(!motor_select_mode(&m,MODE_CLOSED_LOOP));
        in=input(++now,0); motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_FAULT);
        motor_step(&m,&in,ACTION_CLEAR_FAULT,&out); assert(m.state==MOTOR_DISABLED && !out.enable);
        motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_OPEN_IDLE && !out.enable);
    }
    motor_t m; motor_output_t out; select_open(&m);
    motor_input_t in=input(1,1); motor_step(&m,&in,ACTION_ARM,&out);
    assert(m.state==MOTOR_DISABLED && !out.pwm); /* Zero-start interlock. */
    in=input(2,0); motor_step(&m,&in,ACTION_CALIBRATE,&out); assert(m.state==MOTOR_DISABLED);
    motor_step(&m,&in,ACTION_ZERO,&out); assert(!m.zeroed);
    in.source=SOURCE_ETHERNET; motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_DISABLED);
    in=input(3,0); motor_step(&m,&in,ACTION_ARM,&out);
    in=input(4,1); motor_step(&m,&in,ACTION_NONE,&out); assert(m.state==MOTOR_OPEN_WAKE && !out.pwm);
    in.driver_good=false;
    for (unsigned i=0;i<9;++i) { ++in.now_ms; in.adc_ms=in.foreground_ms=in.now_ms; motor_step(&m,&in,ACTION_NONE,&out); }
    assert(m.state==MOTOR_OPEN_WAKE && !out.pwm); /* Expected wake nFAULT does not energize. */
    ++in.now_ms; in.adc_ms=in.foreground_ms=in.now_ms; motor_step(&m,&in,ACTION_NONE,&out);
    assert(m.state==MOTOR_FAULT && !out.enable);
    uint32_t now=UINT32_MAX-100u; start(&m,&now,&out); /* Timed stages across millis wrap. */
    in=input(++now,1); in.stop=true; motor_step(&m,&in,ACTION_ARM,&out);
    assert(m.state==MOTOR_DISABLED && !out.pwm && !out.enable);
}
int main(void) {
    test_mapping(); test_spin_and_zero(); test_feedback_independence(); test_faults_and_interlocks();
    puts("PASS: open-loop voltage/RPM map, one-way field advance, acceleration, continuous wrap, zero coast,\n"
         "encoder independence, fault inhibition, zero-start interlock, wake handling and mode switching.");
    return 0;
}
