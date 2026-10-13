#ifndef DEMO_BOARD_H
#define DEMO_BOARD_H
#include "stm32h7xx_hal.h"
#include "motor_control.h"
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim1, htim7;
void board_init(void);
void board_start_control(void);
void board_pwm_scope_poll(void);
void board_sensors_start(void);
void board_sensor_tick(void);
void board_inputs(motor_input_t *in, uint16_t *adc);
bool board_run_high(void);
void board_apply(const motor_output_t *out);
void board_inhibit(void);
void board_kill(void);
void board_release_stop(void);
void board_trip(motor_fault_t fault);
void board_clear_trip(void);
motor_fault_t board_fault(void);
uint32_t board_us(void);
void board_watchdog_start(void);
void board_watchdog_feed(void);
uint32_t board_lock(void);
void board_unlock(uint32_t mask);
void Error_Handler(void);
#endif
