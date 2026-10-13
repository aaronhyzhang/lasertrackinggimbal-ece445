/* Host-only board boundary. Never include this directory in an ARM build. */
#ifndef TEST_BOARD_H
#define TEST_BOARD_H
#include "motor_control.h"
#include <stdint.h>
#define GPIOB ((void *)0)
#define GPIOE ((void *)1)
#define GPIO_PIN_1 1u
#define GPIO_PIN_14 14u
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
uint32_t board_lock(void);
void board_unlock(uint32_t mask);
uint32_t HAL_GetTick(void);
uint32_t board_us(void);
void board_inputs(motor_input_t *in,uint16_t *adc);
bool board_run_high(void);
void board_trip(motor_fault_t fault);
void board_clear_trip(void);
void board_kill(void);
void board_release_stop(void);
void board_apply(const motor_output_t *out);
void board_sensor_tick(void);
void board_watchdog_feed(void);
void HAL_GPIO_WritePin(void *port,uint32_t pin,int value);
#endif
