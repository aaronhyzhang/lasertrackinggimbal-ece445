/* Unloaded bench spin test: no encoder, position/velocity PI or current feedback. */
#include "motor_internal.h"
#include "demo_config.h"
#include <math.h>
#define RAD_PER_RPM (6.28318530718f/60.0f)
static float approach(float value,float target,float step) {
    return value+fminf(step,fmaxf(-step,target-value));
}
float motor_open_speed_from_voltage(float voltage) {
    if (!isfinite(voltage) || voltage<0.0f || voltage>3.15f) return NAN;
    if (voltage<=DEMO_OPEN_ZERO_VOLTS) return 0;
    return fminf(voltage/3.0f,1.0f)*DEMO_OPEN_MAX_RPM*RAD_PER_RPM;
}
static void idle(motor_t *m,uint32_t now) {
    motor_enter_state(m,MOTOR_OPEN_IDLE,now);
    m->open_speed=0; m->open_volts=0; m->open_angle=0;
}
static bool active_open(motor_state_t state) {
    return state==MOTOR_OPEN_IDLE || state==MOTOR_OPEN_WAKE ||
           state==MOTOR_OPEN_ALIGN || state==MOTOR_OPEN_RUNNING;
}
void motor_open_step(motor_t *m,const motor_input_t *in,motor_action_t action,motor_output_t *out) {
    bool adc_ok=in->adc_valid && in->now_ms-in->adc_ms<=DEMO_ADC_MAX_AGE_MS;
    bool foreground_ok=in->now_ms-in->foreground_ms<=DEMO_FOREGROUND_MAX_AGE_MS;
    float target=motor_open_speed_from_voltage(in->analog_volts);
    bool command_ok=in->source==SOURCE_ANALOG && isfinite(target);
    m->open_target_speed=isfinite(target)?target:0;
    if (in->hard_fault) motor_latch_fault(m,in->external_fault,in->now_ms);
    if (in->stop || action==ACTION_DISABLE) {
        if (m->state!=MOTOR_FAULT) motor_enter_state(m,MOTOR_DISABLED,in->now_ms);
        m->open_speed=0; m->open_volts=0; m->open_angle=0; return;
    }
    if (active_open(m->state)) {
        if (!adc_ok || !isfinite(target)) motor_latch_fault(m,FAULT_ADC,in->now_ms);
        else if (!command_ok) motor_latch_fault(m,FAULT_COMMAND,in->now_ms);
        else if (!foreground_ok) motor_latch_fault(m,FAULT_FOREGROUND,in->now_ms);
        else if ((m->state==MOTOR_OPEN_ALIGN || m->state==MOTOR_OPEN_RUNNING) && !in->driver_good)
            motor_latch_fault(m,FAULT_DRIVER,in->now_ms);
    }
    if (action==ACTION_CLEAR_FAULT && m->state==MOTOR_FAULT && !in->hard_fault &&
        adc_ok && command_ok && foreground_ok) {
        m->fault=FAULT_NONE; motor_enter_state(m,MOTOR_DISABLED,in->now_ms);
    }
    if (action==ACTION_ARM && motor_inhibited(m) && adc_ok && command_ok && foreground_ok &&
        in->analog_volts<=DEMO_OPEN_ARM_MAX_VOLTS) idle(m,in->now_ms);
    uint32_t elapsed=in->now_ms-m->stage_ms;
    switch (m->state) {
    case MOTOR_OPEN_IDLE:
        if (target>0) { motor_enter_state(m,MOTOR_OPEN_WAKE,in->now_ms); out->enable=true; }
        break;
    case MOTOR_OPEN_WAKE:
        if (target==0) { idle(m,in->now_ms); break; }
        out->enable=true; /* Hardware break stays active; MOE clear while waking. */
        if (elapsed>=10u) {
            if (!in->driver_good) motor_latch_fault(m,FAULT_DRIVER,in->now_ms);
            else motor_enter_state(m,MOTOR_OPEN_ALIGN,in->now_ms);
        }
        break;
    case MOTOR_OPEN_ALIGN:
        if (target==0) { idle(m,in->now_ms); break; }
        m->open_volts=fminf(DEMO_ALIGN_VOLTS,DEMO_OPEN_VOLTS)*
            fminf((float)elapsed/(float)DEMO_OPEN_ALIGN_RAMP_MS,1.0f);
        out->enable=out->pwm=true;
        motor_modulate(0,m->open_volts,0,out->duty);
        if (elapsed>=DEMO_OPEN_ALIGN_MS) motor_enter_state(m,MOTOR_OPEN_RUNNING,in->now_ms);
        break;
    case MOTOR_OPEN_RUNNING:
        m->open_speed=approach(m->open_speed,target,DEMO_OPEN_ACCEL_RPM_S*RAD_PER_RPM*0.001f);
        /* At zero after deceleration, coast instead of holding an energized field. */
        if (target==0 && m->open_speed==0) { idle(m,in->now_ms); break; }
        m->open_volts=approach(m->open_volts,DEMO_OPEN_VOLTS,DEMO_OPEN_VOLT_SLEW*0.001f);
        m->open_angle=motor_wrap(m->open_angle+DEMO_POLE_PAIRS*m->open_speed*0.001f);
        out->enable=out->pwm=true;
        motor_modulate(m->open_angle,m->open_volts,0,out->duty);
        break;
    case MOTOR_DISABLED: case MOTOR_READY: case MOTOR_FAULT: break;
    default: motor_latch_fault(m,FAULT_INTERNAL,in->now_ms); break;
    }
    if (m->state==MOTOR_FAULT) {
        out->enable=out->pwm=false; m->open_speed=0; m->open_volts=0;
    }
}
