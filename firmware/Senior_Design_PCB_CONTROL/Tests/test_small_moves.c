#include "position_control.h"

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
    position_control_t s=running(170,1);control_input_t in=sample(&s);
    in.target_deg=180;position_tick(&s,&in,SERVO_NO_ACTION);
    assert(s.state==SERVO_RUNNING && s.move_accel_rpm_s==114.066f);
    /* Same target stays latched even if feedback later moves outside threshold. */
    s.position_deg=100;s.trajectory_deg=100;s.move_accel_rpm_s=90;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.move_accel_rpm_s==90);
    s=running(170,1);in=sample(&s);in.target_deg=220;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.move_accel_rpm_s==s.config.accel_rpm_s);
    s=running(170,1);in=sample(&s);in.target_deg=180;s.trajectory_rpm=10;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.move_accel_rpm_s==s.config.accel_rpm_s);
    s=running(170,1);in=sample(&s);in.target_deg=180;s.config.accel_rpm_s=75;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.move_accel_rpm_s==75);
    position_stop(&s);assert(!s.move_profile_valid);
    s=running(180,1);in=sample(&s);in.target_deg=180;
    for (unsigned j=0;j<260;j++) {
        in.now_us=in.encoder_us=in.current_us=1000+j*1000;
        in.now_ms=in.command_ms=1+j;in.current_sequence=j+1;
        position_tick(&s,&in,SERVO_NO_ACTION);
    }
    assert(s.settled);
    in.target_deg=200;position_tick(&s,&in,SERVO_NO_ACTION);assert(!s.settled);
    s=running(180,1);in=sample(&s);in.target_deg=180;in.experiment_mode=2;
    s.settled_ms=250;position_tick(&s,&in,SERVO_NO_ACTION);assert(!s.settled);
    puts("PASS completion dwell, target change and velocity experiment exclusion");
    puts("PASS small moves, large moves, latched limits, moving retarget, runtime cap and stop");
}
