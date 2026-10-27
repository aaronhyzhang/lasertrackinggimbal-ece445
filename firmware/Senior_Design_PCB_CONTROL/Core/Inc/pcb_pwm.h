#ifndef PCB_PWM_H
#define PCB_PWM_H
#include <stdint.h>
#include "pcb_config.h"
#include "demo_config.h"
/* Caller supplies HAL/CMSIS timer types/macros, or simulated registers in tests. */
#if PCB_TEST_AXIS == 0
#define PCB_PWM_TIMER TIM1
#else
#define PCB_PWM_TIMER TIM8
#endif
/* Used by every STOP/fault/off path, including before peripheral init. Never
 * access a clock-gated timer. Clear BOTH possible bridges, not just selection. */
static inline void pcb_pwm_stop_all(void) {
    if (__HAL_RCC_TIM1_IS_CLK_ENABLED()) {
        CLEAR_BIT(TIM1->DIER,TIM_DIER_BIE); CLEAR_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    }
    if (__HAL_RCC_TIM8_IS_CLK_ENABLED()) {
        CLEAR_BIT(TIM8->DIER,TIM_DIER_BIE); CLEAR_BIT(TIM8->BDTR,TIM_BDTR_MOE);
    }
}
/* Preloaded compares commit on a timer update; UDIS prevents a partial triplet
 * becoming active if an update boundary occurs during these three writes. */
static inline void pcb_pwm_write(const float duty[3]) {
    SET_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_UDIS);
    PCB_PWM_TIMER->CCR1=(uint32_t)(duty[0]*DEMO_PWM_ARR);
    PCB_PWM_TIMER->CCR2=(uint32_t)(duty[1]*DEMO_PWM_ARR);
    PCB_PWM_TIMER->CCR3=(uint32_t)(duty[2]*DEMO_PWM_ARR);
    CLEAR_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_UDIS);
}
#endif
