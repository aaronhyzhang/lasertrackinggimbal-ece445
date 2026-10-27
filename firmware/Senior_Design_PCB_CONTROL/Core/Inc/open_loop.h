#ifndef OPEN_LOOP_H
#define OPEN_LOOP_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOTOR_DISABLED, MOTOR_OPEN_IDLE, MOTOR_OPEN_WAKE, MOTOR_OPEN_ALIGN,
    MOTOR_OPEN_RUNNING, MOTOR_OPEN_COAST, MOTOR_FAULT
} motor_state_t;
typedef enum {
    FAULT_NONE, FAULT_DRIVER, FAULT_ADC, FAULT_FOREGROUND,
    FAULT_CONTROL_TIMING, FAULT_INTERNAL, FAULT_DRIVER_CONFIG, FAULT_CURRENT_CALIBRATION
} motor_fault_t;
typedef enum { ACTION_NONE, ACTION_ARM, ACTION_DISABLE } motor_action_t;

typedef struct {
    uint32_t now_ms, adc_ms, foreground_ms;
    float analog_volts;
    bool adc_valid, driver_good, hard_fault, stop;
    bool current_zero_required,current_zero_ready;
    motor_fault_t external_fault;
} motor_input_t;
typedef struct {
    bool enable, pwm;
    float duty[3];
} motor_output_t;
typedef struct {
    motor_state_t state;
    motor_fault_t fault;
    uint32_t stage_ms;
    float open_angle, open_speed, open_volts, open_target_speed;
    int8_t open_direction;
} motor_t;

void motor_init(motor_t *motor);
void motor_step(motor_t *motor, const motor_input_t *input,
                motor_action_t action, motor_output_t *output);
bool motor_open_neutral(float voltage);
float motor_open_speed_from_voltage(float voltage);
float motor_wrap(float angle);
void motor_modulate(float angle, float volts, float duty[3]);
#endif
