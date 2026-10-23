/* Exercise the actual BSP routing helpers against simulated BSRR registers. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint32_t BSRR; } port_t;
static port_t port_a,port_e,port_g;
#define GPIOA (&port_a)
#define GPIOE (&port_e)
#define GPIOG (&port_g)
enum {
    GPIO_PIN_0=1u<<0, GPIO_PIN_1=1u<<1, GPIO_PIN_2=1u<<2,
    GPIO_PIN_3=1u<<3, GPIO_PIN_4=1u<<4, GPIO_PIN_5=1u<<5,
    GPIO_PIN_6=1u<<6, GPIO_PIN_7=1u<<7, GPIO_PIN_8=1u<<8,
    GPIO_PIN_9=1u<<9, GPIO_PIN_10=1u<<10, GPIO_PIN_11=1u<<11,
    GPIO_PIN_12=1u<<12, GPIO_PIN_13=1u<<13, GPIO_PIN_14=1u<<14,
    GPIO_PIN_15=1u<<15
};
#include "pcb_pins.h"
/* The same stop/update helpers used by the BSP, with simulated timer registers. */
typedef struct { uint32_t DIER,BDTR,CR1,CCR1,CCR2,CCR3; } timer_t;
static timer_t timer1,timer8;
static bool clock1,clock8;
#define TIM1 (&timer1)
#define TIM8 (&timer8)
#define TIM_DIER_BIE (1u<<7)
#define TIM_BDTR_MOE (1u<<15)
#define TIM_CR1_UDIS (1u<<1)
#define SET_BIT(reg,bits) ((reg)|=(bits))
#define CLEAR_BIT(reg,bits) ((reg)&=~(bits))
#define __HAL_RCC_TIM1_IS_CLK_ENABLED() clock1
#define __HAL_RCC_TIM8_IS_CLK_ENABLED() clock8
#include "pcb_pwm.h"
static uint32_t apply(uint32_t level,uint32_t bsrr) {
    return (level|(bsrr&0xffffu))&~(bsrr>>16);
}
int main(void) {
    uint32_t both=(1u<<4)|(1u<<7);
    uint32_t selected=PCB_TEST_AXIS==0 ? 1u<<4 : 1u<<7;
    pcb_driver_enable(true);
    assert(apply(both,port_g.BSRR)==selected); /* Other ENABLE forced LOW. */
    assert((port_g.BSRR&0xffffu)==selected);
    pcb_driver_enable(false);
    assert(apply(both,port_g.BSRR)==0);
    pcb_driver_select(true);
    uint32_t cs0=apply(0,port_e.BSRR)&(1u<<5);
    uint32_t cs1=apply(0,port_a.BSRR)&(1u<<9);
    assert((cs0==0)!=(cs1==0)); /* Never both selected. */
    assert((cs0==0)==(PCB_TEST_AXIS==0));
    assert((cs1==0)==(PCB_TEST_AXIS==1));
    pcb_driver_select(false);
    assert((apply(0,port_e.BSRR)&(1u<<5))!=0);
    assert((apply(0,port_a.BSRR)&(1u<<9))!=0);
    timer_t initial={TIM_DIER_BIE|1u,TIM_BDTR_MOE|3u,0x81u,17,18,19};
    for (unsigned clocks=0;clocks<4;++clocks) {
        clock1=(clocks&1u)!=0; clock8=(clocks&2u)!=0;
        timer1=timer8=initial;
        pcb_pwm_stop_all();
        assert(timer1.BDTR==(clock1?3u:initial.BDTR));
        assert(timer8.BDTR==(clock8?3u:initial.BDTR));
        assert(timer1.DIER==(clock1?1u:initial.DIER));
        assert(timer8.DIER==(clock8?1u:initial.DIER));
    }
    timer1=timer8=initial;
    const float duty[3]={0.25f,0.5f,0.75f};
    pcb_pwm_write(duty);
    timer_t *selected_timer=PCB_TEST_AXIS==0?TIM1:TIM8;
    timer_t *inactive_timer=PCB_TEST_AXIS==0?TIM8:TIM1;
    assert(PCB_PWM_TIMER==selected_timer);
    assert(selected_timer->CCR1==625 && selected_timer->CCR2==1250 && selected_timer->CCR3==1875);
    assert(selected_timer->CR1==initial.CR1);
    assert(selected_timer->BDTR==initial.BDTR); /* Duty writes cannot authorize PWM. */
    assert(inactive_timer->CCR1==17 && inactive_timer->CCR2==18 && inactive_timer->CCR3==19);
    assert(inactive_timer->CR1==initial.CR1);
    clock1=clock8=true; pcb_pwm_stop_all(); pcb_driver_enable(false);
    assert((timer1.BDTR&TIM_BDTR_MOE)==0 && (timer8.BDTR&TIM_BDTR_MOE)==0);
    assert(apply(both,port_g.BSRR)==0);
    printf("PASS: axis %d actual ENABLE/CS/PWM routes; inactive timer untouched; all clock combinations inhibit both MOE/BIE and both enables.\n",PCB_TEST_AXIS);
}
