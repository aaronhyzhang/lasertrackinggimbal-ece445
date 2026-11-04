/* Generated-angle sine PWM. Voltage commands, not current regulation. */
#include "open_loop.h"
#include "demo_config.h"
#include <math.h>

float motor_wrap(float angle) {
    const float turn = 6.28318530718f;
    angle = fmodf(angle, turn);
    return angle < 0.0f ? angle + turn : angle;
}

void motor_modulate(float angle, float volts, float duty[3]) {
    float alpha = volts * cosf(angle);
    float beta = volts * sinf(angle);
    float phase[3] = {
        alpha,
        -0.5f * alpha + 0.8660254038f * beta,
        -0.5f * alpha - 0.8660254038f * beta
    };
    for (unsigned i = 0; i < 3; ++i)
        duty[i] = fminf(0.95f, fmaxf(0.05f, 0.5f + phase[i] / DEMO_BUS_VOLTS));
}
