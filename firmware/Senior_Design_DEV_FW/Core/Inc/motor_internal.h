#ifndef MOTOR_INTERNAL_H
#define MOTOR_INTERNAL_H
#include "motor_control.h"
/* Internal control-module helpers; called only by the single control owner. */
void motor_enter_state(motor_t *m,motor_state_t state,uint32_t now);
void motor_latch_fault(motor_t *m,motor_fault_t fault,uint32_t now);
void motor_open_step(motor_t *m,const motor_input_t *in,motor_action_t action,motor_output_t *out);
#endif
