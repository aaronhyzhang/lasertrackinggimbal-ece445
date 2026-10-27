#include "position_control.h"
#include "control_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    position_control_t s;position_init(&s);
    assert(fabsf(s.config.velocity_kp-CONTROL_VELOCITY_KP_V)<0.00001f);
    assert(fabsf(s.config.velocity_ki-CONTROL_VELOCITY_KI_V)<0.00001f);
    assert(s.config.run_volts==6.0f);
#if PCB_TEST_AXIS == 0
    assert(CONTROL_ENCODER_POSITIVE_SIGN==0 && CONTROL_COMMAND_MAX_DEG==358);
    assert(s.config.position_kp==5.5f);
    assert(s.config.max_rpm==110.0f && s.config.accel_rpm_s==150.0f);
    assert(CONTROL_MOTION_ENABLED==1);
#else
    assert(CONTROL_ENCODER_POSITIVE_SIGN==-1 && CONTROL_COMMAND_MAX_DEG==337);
    assert(s.config.position_kp==5.0f);
    assert(s.config.max_rpm==110.0f && s.config.accel_rpm_s==199.502f);
    assert(CONTROL_MOTION_ENABLED==0);
#endif
    s.config.velocity_kp=0.9f;position_init(&s);
    assert(fabsf(s.config.velocity_kp-CONTROL_VELOCITY_KP_V)<0.00001f);
    puts("PASS selected motor reset defaults and motion enable; RAM SET is not persistent");
}
