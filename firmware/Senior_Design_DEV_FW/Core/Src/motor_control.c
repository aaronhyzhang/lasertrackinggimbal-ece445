#include "motor_control.h"
#include "demo_config.h"
#include <math.h>
#include <string.h>
#define PI_F 3.14159265358979323846f
#define TAU_F (2.0f * PI_F)
static float clampf(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }
float motor_wrap(float x) {
    x = fmodf(x, TAU_F);
    return x < 0.0f ? x + TAU_F : x;
}
void motor_modulate(float theta, float vd, float vq, float duty[3]) {
    float c = cosf(theta), s = sinf(theta);
    float alpha = vd*c - vq*s, beta = vd*s + vq*c;
    float phase[3] = {alpha, -0.5f*alpha + 0.8660254038f*beta,
                            -0.5f*alpha - 0.8660254038f*beta};
    for (unsigned i = 0; i < 3; ++i)
        duty[i] = clampf(0.5f + phase[i]/DEMO_BUS_VOLTS, 0.05f, 0.95f);
}
void motor_init(motor_t *m) {
    memset(m, 0, sizeof(*m)); m->direction = 1; m->state = MOTOR_DISABLED;
}
static bool active(motor_state_t s) {
    return s != MOTOR_DISABLED && s != MOTOR_READY && s != MOTOR_FAULT;
}
static void enter(motor_t *m, motor_state_t s, uint32_t now) {
    m->state = s; m->stage_ms = now; m->integral = 0; m->effort = 0;
}
static void trip(motor_t *m, motor_fault_t f, uint32_t now) {
    if (m->state != MOTOR_FAULT) { m->fault = f; enter(m, MOTOR_FAULT, now); }
}
static void encoder_update(motor_t *m, const motor_input_t *in) {
    if (!in->encoder_valid) return;
    uint16_t raw = in->raw_angle & 4095u;
    if (!m->encoder_initialized) {
        m->encoder_initialized = true; m->last_raw = raw;
        m->raw_unwrapped = (float)raw * (TAU_F/4096.0f);
        m->last_encoder_ms = in->encoder_ms; return;
    }
    if (in->encoder_ms == m->last_encoder_ms) return;
    int32_t delta = (int32_t)raw - m->last_raw;
    if (delta > 2048) delta -= 4096;
    if (delta < -2048) delta += 4096;
    float dr = (float)delta * (TAU_F/4096.0f);
    uint32_t elapsed = in->encoder_ms - m->last_encoder_ms;
    /* Reject impossible sample jumps instead of silently selecting another turn. */
    if (elapsed > DEMO_ENCODER_MAX_AGE_MS || fabsf(dr) > 0.12f) {
        m->calibrated = false; m->zeroed = false;
        m->last_raw = raw; m->last_encoder_ms = in->encoder_ms;
        if (active(m->state)) trip(m, FAULT_ENCODER, in->now_ms);
        return;
    }
    m->raw_unwrapped += dr;
    float v = m->direction * dr / ((float)elapsed * 0.001f);
    float a = ((float)elapsed * 0.001f)/(DEMO_VELOCITY_LPF + (float)elapsed*0.001f);
    m->velocity += a*(v - m->velocity);
    m->last_raw = raw; m->last_encoder_ms = in->encoder_ms;
}
void motor_step(motor_t *m, const motor_input_t *in, motor_action_t action,
                motor_output_t *out) {
    memset(out, 0, sizeof(*out));
    out->duty[0] = out->duty[1] = out->duty[2] = 0.5f;
    encoder_update(m, in);
    m->position = m->direction * (m->raw_unwrapped - m->zero_raw);
    bool encoder_ok = in->encoder_valid && in->magnet_good &&
        in->now_ms - in->encoder_ms <= DEMO_ENCODER_MAX_AGE_MS &&
        in->now_ms - in->health_ms <= DEMO_HEALTH_MAX_AGE_MS;
    bool adc_ok = in->adc_valid && in->now_ms-in->adc_ms <= DEMO_ADC_MAX_AGE_MS;
    bool command_ok = in->source == SOURCE_ANALOG ||
        in->now_ms-in->command_ms <= DEMO_COMMAND_MAX_AGE_MS;
    if (in->hard_fault) trip(m, in->external_fault, in->now_ms);
    if (in->stop || action == ACTION_DISABLE) {
        if (m->state != MOTOR_FAULT) enter(m, m->calibrated ? MOTOR_READY : MOTOR_DISABLED, in->now_ms);
        m->reference_velocity = 0; return;
    }
    if (active(m->state)) {
        if (!encoder_ok) trip(m, FAULT_ENCODER, in->now_ms);
        else if (in->now_ms-in->foreground_ms > DEMO_FOREGROUND_MAX_AGE_MS)
            trip(m, FAULT_FOREGROUND, in->now_ms);
        else if (m->state != MOTOR_CAL_WAKE && m->state != MOTOR_ARM_WAKE && !in->driver_good)
            trip(m, FAULT_DRIVER, in->now_ms);
        else if ((m->state == MOTOR_RUNNING || m->state == MOTOR_ARM_WAKE) && !command_ok)
            trip(m, FAULT_COMMAND, in->now_ms);
        else if ((m->state == MOTOR_RUNNING || m->state == MOTOR_ARM_WAKE) &&
                 in->source == SOURCE_ANALOG && !adc_ok) trip(m, FAULT_ADC, in->now_ms);
    }
    if (action == ACTION_CLEAR_FAULT && m->state == MOTOR_FAULT && !in->hard_fault && encoder_ok) {
        m->fault = FAULT_NONE; m->calibrated = false; m->zeroed = false;
        enter(m, MOTOR_DISABLED, in->now_ms);
    }
    if (action == ACTION_CALIBRATE && (m->state == MOTOR_DISABLED || m->state == MOTOR_READY) && encoder_ok) {
        m->calibrated = false; m->zeroed = false;
        enter(m, MOTOR_CAL_WAKE, in->now_ms);
    }
    if (action == ACTION_ZERO && m->state == MOTOR_READY && encoder_ok) {
        m->zero_raw = m->raw_unwrapped; m->position = 0; m->zeroed = true;
    }
    if (action == ACTION_ARM && m->state == MOTOR_READY && m->zeroed && encoder_ok &&
        command_ok && (in->source != SOURCE_ANALOG || adc_ok) &&
        m->position >= -0.02f && m->position <= TAU_F+0.02f) {
        m->reference = clampf(m->position, 0, TAU_F); m->reference_velocity = 0;
        m->velocity = 0; enter(m, MOTOR_ARM_WAKE, in->now_ms);
    }
    uint32_t t = in->now_ms - m->stage_ms;
    switch (m->state) {
    case MOTOR_CAL_WAKE: case MOTOR_ARM_WAKE:
        out->enable = true; /* Break remains active; MOE stays clear throughout wake. */
        if (t >= 10u) {
            if (!in->driver_good) trip(m, FAULT_DRIVER, in->now_ms);
            else enter(m, m->state == MOTOR_CAL_WAKE ? MOTOR_CAL_ALIGN : MOTOR_RUNNING, in->now_ms);
        }
        break;
    case MOTOR_CAL_ALIGN:
        out->enable = out->pwm = true;
        motor_modulate(0, DEMO_ALIGN_VOLTS*clampf((float)t/500.0f,0,1),0,out->duty);
        if (t >= 2000u) { m->sweep_start = m->raw_unwrapped; enter(m,MOTOR_CAL_SWEEP,in->now_ms); }
        break;
    case MOTOR_CAL_SWEEP:
        out->enable = out->pwm = true;
        motor_modulate(0.5f*PI_F*clampf((float)t/1500.0f,0,1),DEMO_ALIGN_VOLTS,0,out->duty);
        if (t >= 2500u) {
            float moved = m->raw_unwrapped - m->sweep_start;
            float expected = 0.5f*PI_F/DEMO_POLE_PAIRS;
            if (fabsf(moved) < 0.5f*expected || fabsf(moved) > 1.5f*expected)
                trip(m,FAULT_CALIBRATION,in->now_ms);
            else { m->direction = moved > 0 ? 1 : -1; enter(m,MOTOR_CAL_RETURN,in->now_ms); }
        }
        break;
    case MOTOR_CAL_RETURN:
        out->enable = out->pwm = true;
        motor_modulate(0.5f*PI_F*(1.0f-clampf((float)t/1500.0f,0,1)),DEMO_ALIGN_VOLTS,0,out->duty);
        if (t >= 2500u) {
            if (fabsf(m->raw_unwrapped-m->sweep_start) > 0.035f) trip(m,FAULT_CALIBRATION,in->now_ms);
            else {
                m->electrical_offset = motor_wrap(-m->direction*DEMO_POLE_PAIRS*m->raw_unwrapped);
                m->calibrated = true; m->velocity = 0; enter(m,MOTOR_READY,in->now_ms);
            }
            out->enable = out->pwm = false;
        }
        break;
    case MOTOR_RUNNING: {
        if (!isfinite(in->target_rad) || in->target_rad < 0 || in->target_rad > TAU_F) {
            trip(m,FAULT_COMMAND,in->now_ms); break;
        }
        if (m->position < -0.0873f || m->position > TAU_F+0.0873f) {
            trip(m,FAULT_TRAVEL,in->now_ms); break;
        }
        m->target = in->target_rad;
        float distance = m->target - m->reference;
        float dv=DEMO_ACCEL*0.001f;
        float braking = sqrtf(2.0f*DEMO_ACCEL*fabsf(distance)+0.25f*dv*dv)-0.5f*dv;
        float desired = copysignf(fminf(DEMO_MAX_SPEED,braking),distance);
        float old_reference_velocity=m->reference_velocity;
        m->reference_velocity += clampf(desired-m->reference_velocity,-DEMO_ACCEL*0.001f,DEMO_ACCEL*0.001f);
        float step = m->reference_velocity*0.001f;
        if (distance*step>=0 && fabsf(distance)<=fabsf(step) && fabsf(old_reference_velocity)<=dv) {
            m->reference = m->target; m->reference_velocity = 0;
        }
        else m->reference = clampf(m->reference+step,0,TAU_F);
        if (fabsf(m->reference-m->position)>DEMO_MAX_FOLLOWING_ERROR) {
            trip(m,FAULT_TRACKING,in->now_ms); break;
        }
        float demand = clampf(DEMO_POSITION_KP*(m->reference-m->position)+m->reference_velocity,
                              -DEMO_MAX_SPEED, DEMO_MAX_SPEED);
        float error = demand - m->velocity;
        float candidate = clampf(m->integral + DEMO_VELOCITY_KI*error*0.001f,-DEMO_MAX_Q_VOLTS,DEMO_MAX_Q_VOLTS);
        float unsaturated = DEMO_VELOCITY_KP*error + candidate;
        /* Conditional integration: do not wind up against the effort clamp. */
        if (fabsf(unsaturated) <= DEMO_MAX_Q_VOLTS || error*unsaturated < 0) m->integral = candidate;
        m->effort = clampf(DEMO_VELOCITY_KP*error+m->integral,-DEMO_MAX_Q_VOLTS,DEMO_MAX_Q_VOLTS);
        out->enable = out->pwm = true;
        float theta = motor_wrap(m->direction*DEMO_POLE_PAIRS*m->raw_unwrapped+m->electrical_offset);
        motor_modulate(theta,0,m->effort,out->duty); break;
    }
    default: break;
    }
    if (m->state == MOTOR_FAULT) { out->enable = out->pwm = false; m->effort = 0; }
}
const char *motor_state_name(motor_state_t s) {
    static const char *const names[] = {"DISABLED","CAL_WAKE","CAL_ALIGN","CAL_SWEEP","CAL_RETURN","READY","ARM_WAKE","RUNNING","FAULT"};
    return (unsigned)s < sizeof(names)/sizeof(names[0]) ? names[s] : "INVALID";
}
const char *motor_fault_name(motor_fault_t f) {
    static const char *const names[] = {"NONE","DRIVER","ENCODER","ADC","FOREGROUND","COMMAND_TIMEOUT","CALIBRATION","TRAVEL","CONTROL_TIMING","INTERNAL","TRACKING"};
    return (unsigned)f < sizeof(names)/sizeof(names[0]) ? names[f] : "INVALID";
}
