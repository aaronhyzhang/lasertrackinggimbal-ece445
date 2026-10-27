/* Encoder-free signed field-speed bench test; no position/current feedback. */
#include "open_loop.h"
#include <string.h>
#include "demo_config.h"
#include <math.h>
#define RAD_PER_RPM (6.28318530718f/60.0f)
void motor_init(motor_t *m) {
    memset(m, 0, sizeof(*m));
    m->state = MOTOR_DISABLED;
}
static void motor_enter_state(motor_t *m, motor_state_t state, uint32_t now) {
    m->state = state;
    m->stage_ms = now;
}
static void motor_latch_fault(motor_t *m, motor_fault_t fault, uint32_t now) {
    if (m->state != MOTOR_FAULT) {
        m->fault = fault;
        motor_enter_state(m, MOTOR_FAULT, now);
    }
}
static float approach(float value,float target,float step) {
    return value+fminf(step,fmaxf(-step,target-value));
}
bool motor_open_neutral(float voltage) {
    return isfinite(voltage) && voltage>=DEMO_OPEN_NEUTRAL_LOW && voltage<=DEMO_OPEN_NEUTRAL_HIGH;
}
float motor_open_speed_from_voltage(float voltage) {
    if (!isfinite(voltage) || voltage<0.0f || voltage>3.15f) return NAN;
    if (motor_open_neutral(voltage)) return 0;
    float fraction;
    if (voltage<DEMO_OPEN_NEUTRAL_LOW)
        fraction=(voltage-DEMO_OPEN_NEUTRAL_LOW)/DEMO_OPEN_NEUTRAL_LOW;
    else
        fraction=fminf((voltage-DEMO_OPEN_NEUTRAL_HIGH)/(3.0f-DEMO_OPEN_NEUTRAL_HIGH),1.0f);
    return fraction*DEMO_OPEN_MAX_RPM*RAD_PER_RPM;
}
static int8_t direction(float speed) { return speed<0?-1:speed>0?1:0; }
static void idle(motor_t *m,uint32_t now) {
    motor_enter_state(m,MOTOR_OPEN_IDLE,now);
    m->open_speed=0; m->open_volts=0; m->open_angle=0; m->open_direction=0;
}
static void coast(motor_t *m,uint32_t now) {
    motor_enter_state(m,MOTOR_OPEN_COAST,now);
    m->open_speed=0; m->open_volts=0; m->open_angle=0; m->open_direction=0;
}
static bool active_open(motor_state_t state) {
    return state==MOTOR_OPEN_IDLE || state==MOTOR_OPEN_WAKE ||
           state==MOTOR_OPEN_ALIGN || state==MOTOR_OPEN_RUNNING || state==MOTOR_OPEN_COAST;
}
void motor_step(motor_t *m,const motor_input_t *in,motor_action_t action,motor_output_t *out) {
    memset(out, 0, sizeof(*out));
    out->duty[0] = out->duty[1] = out->duty[2] = 0.5f;
    bool adc_ok=in->adc_valid && in->now_ms-in->adc_ms<=DEMO_ADC_MAX_AGE_MS;
    bool foreground_ok=in->now_ms-in->foreground_ms<=DEMO_FOREGROUND_MAX_AGE_MS;
    float target=motor_open_speed_from_voltage(in->analog_volts);
    m->open_target_speed=isfinite(target)?target:0;
    if (in->hard_fault) motor_latch_fault(m,in->external_fault,in->now_ms);
    if (in->stop || action==ACTION_DISABLE) {
        if (m->state!=MOTOR_FAULT) motor_enter_state(m,MOTOR_DISABLED,in->now_ms);
        m->open_speed=0; m->open_volts=0; m->open_angle=0; m->open_direction=0; return;
    }
    if (active_open(m->state)) {
        if (!adc_ok || !isfinite(target)) motor_latch_fault(m,FAULT_ADC,in->now_ms);
        else if (!foreground_ok) motor_latch_fault(m,FAULT_FOREGROUND,in->now_ms);
        else if ((m->state==MOTOR_OPEN_ALIGN || m->state==MOTOR_OPEN_RUNNING) && !in->driver_good)
            motor_latch_fault(m,FAULT_DRIVER,in->now_ms);
    }
    if (action==ACTION_ARM && m->state==MOTOR_DISABLED && adc_ok && isfinite(target) && foreground_ok &&
        motor_open_neutral(in->analog_volts)) idle(m,in->now_ms);
    uint32_t elapsed=in->now_ms-m->stage_ms;
    switch (m->state) {
    case MOTOR_OPEN_IDLE:
        if (target!=0) {
            m->open_direction=direction(target);
            motor_enter_state(m,MOTOR_OPEN_WAKE,in->now_ms); out->enable=true;
        }
        break;
    case MOTOR_OPEN_WAKE:
        if (target==0) { idle(m,in->now_ms); break; }
        m->open_direction=direction(target); /* No excitation yet: latest direction is safe. */
        out->enable=true;
        if (elapsed>=DEMO_OPEN_WAKE_MS) {
            if (!in->driver_good) motor_latch_fault(m,FAULT_DRIVER,in->now_ms);
            else if (!in->current_zero_required || in->current_zero_ready)
                motor_enter_state(m,MOTOR_OPEN_ALIGN,in->now_ms);
            else if (elapsed>=DEMO_CURRENT_ZERO_TIMEOUT_MS)
                motor_latch_fault(m,FAULT_CURRENT_CALIBRATION,in->now_ms);
        }
        break;
    case MOTOR_OPEN_ALIGN:
        if (target==0 || direction(target)!=m->open_direction) { coast(m,in->now_ms); break; }
        m->open_volts=fminf(DEMO_ALIGN_VOLTS,DEMO_OPEN_VOLTS)*
            fminf((float)elapsed/(float)DEMO_OPEN_ALIGN_RAMP_MS,1.0f);
        out->enable=out->pwm=true;
        motor_modulate(0,m->open_volts,out->duty);
        if (elapsed>=DEMO_OPEN_ALIGN_MS) motor_enter_state(m,MOTOR_OPEN_RUNNING,in->now_ms);
        break;
    case MOTOR_OPEN_RUNNING: {
        bool reverse=target!=0 && direction(target)!=m->open_direction;
        float ramp_target=reverse?0:target;
        m->open_speed=approach(m->open_speed,ramp_target,DEMO_OPEN_ACCEL_RPM_S*RAD_PER_RPM*0.001f);
        /* Never advance directly through zero into the opposite direction. */
        if (m->open_speed==0 && (target==0 || reverse)) { coast(m,in->now_ms); break; }
        m->open_volts=approach(m->open_volts,DEMO_OPEN_VOLTS,DEMO_OPEN_VOLT_SLEW*0.001f);
        m->open_angle=motor_wrap(m->open_angle+DEMO_POLE_PAIRS*m->open_speed*0.001f);
        out->enable=out->pwm=true;
        motor_modulate(m->open_angle,m->open_volts,out->duty);
        break;
    }
    case MOTOR_OPEN_COAST:
        if (elapsed>=DEMO_OPEN_COAST_MS) idle(m,in->now_ms);
        break; /* All gates/ENABLE off; pause is not proof the actual rotor stopped. */
    case MOTOR_DISABLED: case MOTOR_FAULT: break;
    default: motor_latch_fault(m,FAULT_INTERNAL,in->now_ms); break;
    }
    if (m->state==MOTOR_FAULT) {
        out->enable=out->pwm=false; m->open_speed=0; m->open_volts=0; m->open_direction=0;
    }
}
