#include "motor_control.h"
#include "demo_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define TAU 6.28318530718f
#define NEUTRAL 1.5f
static motor_input_t input(uint32_t now,float voltage) {
    motor_input_t in={0}; in.now_ms=now; in.adc_ms=in.foreground_ms=now;
    in.adc_valid=in.driver_good=true; in.source=SOURCE_ANALOG; in.analog_volts=voltage;
    return in; /* Encoder deliberately absent. */
}
static void select_open(motor_t *m) {
    motor_init(m); assert(motor_select_mode(m,MODE_OPEN_LOOP));
    assert(m->state==MOTOR_DISABLED && !m->calibrated && !m->zeroed);
}
static void start(motor_t *m,uint32_t *now,motor_output_t *out,int sign) {
    select_open(m); motor_input_t in=input(++*now,NEUTRAL);
    motor_step(m,&in,ACTION_ARM,out); assert(m->state==MOTOR_OPEN_IDLE && !out->enable && !out->pwm);
    for (unsigned i=0;i<DEMO_OPEN_ALIGN_MS+20;++i) {
        in=input(++*now,sign>0?3:0); motor_step(m,&in,ACTION_NONE,out);
    }
    assert(m->state==MOTOR_OPEN_RUNNING && out->pwm && m->open_direction==sign);
}
static void test_mapping(void) {
    float max=DEMO_OPEN_MAX_RPM*TAU/60;
    assert(motor_open_neutral(1.3f) && motor_open_neutral(1.5f) && motor_open_neutral(1.7f));
    assert(!motor_open_neutral(0.04f) && !motor_open_neutral(1.29f) && !motor_open_neutral(1.71f));
    assert(!motor_open_neutral(NAN));
    assert(motor_open_speed_from_voltage(1.3f)==0 && motor_open_speed_from_voltage(1.5f)==0);
    assert(motor_open_speed_from_voltage(1.7f)==0);
    assert(fabsf(motor_open_speed_from_voltage(0)+max)<1e-6f);
    assert(fabsf(motor_open_speed_from_voltage(3)-max)<1e-6f);
    assert(fabsf(motor_open_speed_from_voltage(0.65f)+max/2)<1e-5f);
    assert(fabsf(motor_open_speed_from_voltage(2.35f)-max/2)<1e-5f);
    assert(motor_open_speed_from_voltage(3.1f)==motor_open_speed_from_voltage(3));
    for (unsigned i=0;i<130;++i) {
        float v=(float)i/100;
        assert(fabsf(motor_open_speed_from_voltage(v)+motor_open_speed_from_voltage(3-v))<3e-6f);
    }
    assert(isnan(motor_open_speed_from_voltage(NAN)));
    assert(isnan(motor_open_speed_from_voltage(-0.01f)) && isnan(motor_open_speed_from_voltage(3.2f)));
}
static void test_spin_and_neutral(int sign) {
    motor_t m; motor_output_t out; uint32_t now=0; start(&m,&now,&out,sign);
    float dv=DEMO_OPEN_ACCEL_RPM_S*TAU/60*0.001f;
    unsigned wraps=0;
    for (unsigned i=0;i<12000;++i) {
        float old_speed=m.open_speed,old_angle=m.open_angle;
        motor_input_t in=input(++now,sign>0?3:0); motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state==MOTOR_OPEN_RUNNING && out.enable && out.pwm && m.open_direction==sign);
        assert(m.open_speed*sign>=0 && fabsf(m.open_speed)<=DEMO_OPEN_MAX_RPM*TAU/60+1e-5f);
        assert(fabsf(m.open_speed-old_speed)<=dv+1e-5f);
        float increment=motor_wrap(m.open_angle-old_angle+TAU/2)-TAU/2;
        if ((sign>0 && m.open_angle<old_angle) || (sign<0 && m.open_angle>old_angle)) ++wraps;
        assert(fabsf(increment-DEMO_POLE_PAIRS*m.open_speed*0.001f)<3e-6f);
        assert(m.open_angle>=0 && m.open_angle<TAU && m.open_volts<=DEMO_OPEN_VOLTS);
        assert(m.integral==0 && !m.calibrated && !m.zeroed);
        for (unsigned phase=0;phase<3;++phase) assert(out.duty[phase]>=0.05f && out.duty[phase]<=0.95f);
    }
    assert(wraps>10 && fabsf(m.open_speed-sign*DEMO_OPEN_MAX_RPM*TAU/60)<1e-5f);
    unsigned stop_ticks=(unsigned)ceilf(DEMO_OPEN_MAX_RPM/DEMO_OPEN_ACCEL_RPM_S*1000)+DEMO_OPEN_COAST_MS+50u;
    bool coast_seen=false;
    for (unsigned i=0;i<stop_ticks && m.state!=MOTOR_OPEN_IDLE;++i) {
        float old=m.open_speed; motor_input_t in=input(++now,NEUTRAL);
        motor_step(&m,&in,ACTION_NONE,&out); assert(fabsf(m.open_speed-old)<=dv+1e-5f);
        if (m.state==MOTOR_OPEN_COAST) { coast_seen=true; assert(!out.enable && !out.pwm); }
    }
    assert(coast_seen && m.state==MOTOR_OPEN_IDLE && m.open_speed==0 && !out.enable && !out.pwm);
    assert(!motor_select_mode(&m,MODE_CLOSED_LOOP));
    motor_input_t in=input(++now,NEUTRAL); motor_step(&m,&in,ACTION_DISABLE,&out);
    assert(m.state==MOTOR_DISABLED && motor_select_mode(&m,MODE_CLOSED_LOOP));
    motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_DISABLED && !out.enable);
}
static void test_reversal(int sign) {
    motor_t m; motor_output_t out; uint32_t now=0; start(&m,&now,&out,sign);
    motor_input_t in;
    for (unsigned i=0;i<1000;++i) { in=input(++now,sign>0?3:0); motor_step(&m,&in,ACTION_NONE,&out); }
    bool coast_seen=false,align_seen=false,finished=false; uint32_t coast_at=0;
    for (unsigned i=0;i<6000;++i) {
        float old=m.open_speed; in=input(++now,sign>0?0:3); motor_step(&m,&in,ACTION_NONE,&out);
        if (!coast_seen && m.state==MOTOR_OPEN_RUNNING) {
            assert(m.open_speed*sign>=0 && fabsf(m.open_speed)<=fabsf(old)+1e-6f);
        }
        if (m.state==MOTOR_OPEN_COAST) {
            if (!coast_seen) coast_at=now;
            coast_seen=true; assert(!out.enable && !out.pwm && m.open_speed==0);
        }
        if (coast_seen && m.state!=MOTOR_OPEN_COAST) assert(now-coast_at>=DEMO_OPEN_COAST_MS);
        if (coast_seen && m.state==MOTOR_OPEN_ALIGN) { align_seen=true; assert(m.open_direction==-sign); }
        if (coast_seen && m.state==MOTOR_OPEN_RUNNING && m.open_speed*sign<0) { finished=true; break; }
    }
    assert(coast_seen && align_seen && finished);
    /* Disable during an opposite request never schedules an automatic restart. */
    in=input(++now,sign>0?3:0); in.stop=true; motor_step(&m,&in,ACTION_ARM,&out);
    assert(m.state==MOTOR_DISABLED && !out.enable && !out.pwm);
}
static void test_alignment_cancel(void) {
    motor_t m; motor_output_t out; uint32_t now=0; select_open(&m);
    motor_input_t in=input(++now,NEUTRAL); motor_step(&m,&in,ACTION_ARM,&out);
    for (unsigned i=0;i<30;++i) { in=input(++now,3); motor_step(&m,&in,ACTION_NONE,&out); }
    assert(m.state==MOTOR_OPEN_ALIGN);
    in=input(++now,0); motor_step(&m,&in,ACTION_NONE,&out);
    assert(m.state==MOTOR_OPEN_COAST && !out.enable && !out.pwm);
    in=input(++now,0); in.adc_valid=false; motor_step(&m,&in,ACTION_NONE,&out);
    assert(m.state==MOTOR_FAULT && !out.pwm); /* Health checks remain active while coasting. */
}
static void test_feedback_independence(void) {
    motor_t a,b; motor_output_t oa,ob; uint32_t now=0; select_open(&a); select_open(&b);
    for (unsigned i=0;i<8000;++i) {
        motor_input_t ia=input(++now,i?2.35f:NEUTRAL),ib=ia;
        ib.encoder_valid=ib.magnet_good=true; ib.raw_angle=(uint16_t)(i*173u)&4095u;
        ib.encoder_ms=now; ib.health_ms=now;
        motor_step(&a,&ia,i?ACTION_NONE:ACTION_ARM,&oa); motor_step(&b,&ib,i?ACTION_NONE:ACTION_ARM,&ob);
        assert(a.state==b.state && a.fault==b.fault && oa.enable==ob.enable && oa.pwm==ob.pwm);
        for (unsigned phase=0;phase<3;++phase) assert(oa.duty[phase]==ob.duty[phase]);
    }
}
static void test_faults_and_interlocks(void) {
    for (unsigned kind=0;kind<6;++kind) {
        motor_t m; motor_output_t out; uint32_t now=0; start(&m,&now,&out,1);
        motor_input_t in=input(++now,3);
        if (kind==0) in.adc_valid=false;
        if (kind==1) in.adc_ms=now-DEMO_ADC_MAX_AGE_MS-1;
        if (kind==2) in.foreground_ms=now-DEMO_FOREGROUND_MAX_AGE_MS-1;
        if (kind==3) in.driver_good=false;
        if (kind==4) { in.hard_fault=true; in.external_fault=FAULT_DRIVER; }
        if (kind==5) in.analog_volts=3.2f;
        motor_step(&m,&in,ACTION_NONE,&out);
        assert(m.state==MOTOR_FAULT && !out.enable && !out.pwm && m.open_speed==0);
        assert(!motor_select_mode(&m,MODE_CLOSED_LOOP));
        in=input(++now,NEUTRAL); motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_FAULT);
        motor_step(&m,&in,ACTION_CLEAR_FAULT,&out); assert(m.state==MOTOR_DISABLED);
        motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_OPEN_IDLE && !out.enable);
    }
    motor_t m; motor_output_t out; select_open(&m);
    for (unsigned i=0;i<2;++i) {
        motor_input_t in=input(i+1,i?3:0.04f); motor_step(&m,&in,ACTION_ARM,&out);
        assert(m.state==MOTOR_DISABLED && !out.pwm);
    }
    motor_input_t in=input(3,NEUTRAL); in.source=SOURCE_ETHERNET;
    motor_step(&m,&in,ACTION_ARM,&out); assert(m.state==MOTOR_DISABLED);
    in=input(4,NEUTRAL); motor_step(&m,&in,ACTION_ARM,&out);
    in=input(5,3); motor_step(&m,&in,ACTION_NONE,&out); assert(m.state==MOTOR_OPEN_WAKE && !out.pwm);
    in.driver_good=false;
    for (unsigned i=0;i<9;++i) { ++in.now_ms; in.adc_ms=in.foreground_ms=in.now_ms; motor_step(&m,&in,ACTION_NONE,&out); }
    assert(m.state==MOTOR_OPEN_WAKE && !out.pwm);
    ++in.now_ms; in.adc_ms=in.foreground_ms=in.now_ms; motor_step(&m,&in,ACTION_NONE,&out);
    assert(m.state==MOTOR_FAULT && !out.enable);
    uint32_t now=UINT32_MAX-100u; start(&m,&now,&out,-1);
    in=input(++now,0); in.stop=true; motor_step(&m,&in,ACTION_ARM,&out);
    assert(m.state==MOTOR_DISABLED && !out.pwm);
}
int main(void) {
    test_mapping(); test_spin_and_neutral(1); test_spin_and_neutral(-1);
    test_reversal(1); test_reversal(-1); test_alignment_cancel();
    test_feedback_independence(); test_faults_and_interlocks();
    puts("PASS: symmetric signed RPM, neutral ARM, both field directions/wraps, bounded ramps, coast/realign reversal, stop/fault interlocks and encoder independence.");
    return 0;
}
