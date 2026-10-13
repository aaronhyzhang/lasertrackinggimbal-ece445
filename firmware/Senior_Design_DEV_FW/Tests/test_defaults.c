#include "motor_control.h"
#include "demo_config.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    motor_t m; motor_init(&m);
    assert(m.mode==(DEMO_DEFAULT_OPEN_LOOP?MODE_OPEN_LOOP:MODE_CLOSED_LOOP));
    assert(m.state==MOTOR_DISABLED && !m.calibrated && !m.zeroed);
    printf("PASS: default profile %s boots disabled.\n",motor_mode_name(m.mode));
    return 0;
}
