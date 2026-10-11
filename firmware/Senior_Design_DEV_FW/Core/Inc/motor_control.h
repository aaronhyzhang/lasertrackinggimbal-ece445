#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { MOTOR_DISABLED, MOTOR_CAL_WAKE, MOTOR_CAL_ALIGN,
    MOTOR_CAL_SWEEP, MOTOR_CAL_RETURN, MOTOR_READY, MOTOR_ARM_WAKE,
    MOTOR_RUNNING, MOTOR_FAULT } motor_state_t;
typedef enum { FAULT_NONE, FAULT_DRIVER, FAULT_ENCODER, FAULT_ADC,
    FAULT_FOREGROUND, FAULT_COMMAND, FAULT_CALIBRATION, FAULT_TRAVEL,
    FAULT_CONTROL_TIMING, FAULT_INTERNAL, FAULT_TRACKING } motor_fault_t;
typedef enum { SOURCE_ANALOG, SOURCE_SERIAL, SOURCE_ETHERNET } command_source_t;
typedef enum { ACTION_NONE, ACTION_CALIBRATE, ACTION_ZERO, ACTION_ARM,
    ACTION_DISABLE, ACTION_CLEAR_FAULT } motor_action_t;
typedef struct {
    uint32_t now_ms;
    uint16_t raw_angle;
    uint32_t encoder_ms, health_ms, adc_ms, foreground_ms, command_ms;
    bool encoder_valid, magnet_good, adc_valid, driver_good;
    bool hard_fault, stop;
    motor_fault_t external_fault;
    command_source_t source;
    float target_rad;
} motor_input_t;
typedef struct {
    bool enable, pwm;
    float duty[3];
} motor_output_t;
typedef struct {
    motor_state_t state;
    motor_fault_t fault;
    bool calibrated, zeroed, encoder_initialized;
    int8_t direction;
    uint16_t last_raw;
    uint32_t stage_ms;
    float raw_unwrapped, zero_raw, sweep_start, electrical_offset;
    float position, velocity, reference, reference_velocity;
    float integral, effort, target;
    uint32_t last_encoder_ms;
} motor_t;
void motor_init(motor_t *m);
void motor_step(motor_t *m, const motor_input_t *in, motor_action_t action,
                motor_output_t *out);
float motor_wrap(float angle);
void motor_modulate(float theta, float vd, float vq, float duty[3]);
const char *motor_state_name(motor_state_t state);
const char *motor_fault_name(motor_fault_t fault);
#endif
