#ifndef TEST_BOARD_H
#define TEST_BOARD_H
#include "open_loop.h"
uint32_t board_lock(void);
void board_unlock(uint32_t mask);
uint32_t HAL_GetTick(void);
uint32_t board_us(void);
void board_inputs(motor_input_t *in,uint16_t adc[2]);
bool board_arm_pressed(void);
bool board_stop_pressed(void);
void board_trip(motor_fault_t fault);
void board_kill(void);
void board_release_stop(void);
void board_apply(const motor_output_t *out);
void board_adc_tick(void);
void board_watchdog_feed(void);
void board_service(void);
void board_leds(bool armed,bool waiting,bool fault,uint32_t now);
#endif
