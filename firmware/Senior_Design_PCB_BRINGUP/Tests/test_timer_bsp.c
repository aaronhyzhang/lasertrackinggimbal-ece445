/* Audit actual board.c plus actual vendor TIM/TIMEx/GPIO initialization.
 * Private Windows RAM stands in for registers. No timer counting, preload
 * transfer, output waveforms, external gate driver or IRQ latency is emulated. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include "board.h"
#ifdef NDEBUG
#error Assertions must be enabled
#endif
static uint32_t irq_mask;
#define __get_PRIMASK() irq_mask
#define __disable_irq() ((void)(irq_mask=1))
#define __set_PRIMASK(v) ((void)(irq_mask=(v)))
#define __DSB() ((void)0)
#define SCB_EnableICache() ((void)0)
#undef __HAL_GPIO_EXTI_CLEAR_IT
#define __HAL_GPIO_EXTI_CLEAR_IT(bits) (EXTI->PR1 &= ~(uint32_t)(bits))
#include "board.c"
__declspec(dllimport) void *VirtualAlloc(void *,size_t,unsigned long,unsigned long);
uint32_t HAL_GetTick(void) { return 21; }
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t p,uint32_t s) { (void)irq;(void)p;(void)s; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq) { (void)irq; }
void app_stop(void) { board_kill(); }
static void map(uintptr_t address,size_t bytes) {
    assert(VirtualAlloc((void *)address,bytes,0x3000,4)==(void *)address);
}
int main(void) {
    map(0x58000000u,0x30000);map(0x40000000u,0x20000);
    pins_init();pwm_init();
    TIM_TypeDef *t=PCB_PWM_TIMER;
    assert(t->PSC==0 && t->ARR==2500 && t->RCR==0);
    assert((t->CR1&(TIM_CR1_CMS|TIM_CR1_ARPE|TIM_CR1_CEN))==
           (TIM_COUNTERMODE_CENTERALIGNED1|TIM_CR1_ARPE|TIM_CR1_CEN));
    assert(t->CCR1==1250 && t->CCR2==1250 && t->CCR3==1250);
    assert(t->CCMR1==0x6868 && t->CCMR2==0x68 && t->CCER==0x555);
    assert((t->BDTR&TIM_BDTR_DTG)==50 && (t->BDTR&TIM_BDTR_MOE)==0);
    assert((t->BDTR&(TIM_BDTR_OSSI|TIM_BDTR_OSSR))==(TIM_BDTR_OSSI|TIM_BDTR_OSSR));
    assert((t->BDTR&(TIM_BDTR_BKE|TIM_BDTR_AOE))==0);
    assert(((t->BDTR&TIM_BDTR_BK2E)!=0)==(PCB_TEST_AXIS==0 && PCB_STAGE==PCB_MOTOR_RUN));
    uint32_t init_bdtr=t->BDTR;
    GPIOD->IDR=PCB_STOP;GPIOE->IDR=PCB_NFAULT;
    stop_latched=false;latched_fault=FAULT_NONE;
    wake_requested=driver_ready=true;
    motor_output_t out={.enable=true,.pwm=true,.duty={.51f,.49f,.5f}};
    board_apply(&out);
    assert(bridge_authorized && (t->BDTR&TIM_BDTR_MOE));
    assert(latched_fault==FAULT_NONE && t->CCER==0x555);
    assert(t->CCR1==(uint32_t)(out.duty[0]*2500) && t->CCR2==(uint32_t)(out.duty[1]*2500) && t->CCR3==1250);
#if PCB_STAGE == PCB_PWM_CHECK
    assert(GPIOG->BSRR==(uint32_t)PCB_ENABLES<<16);
#else
    assert(GPIOG->BSRR==(((uint32_t)(PCB_ENABLES&~PCB_SELECTED_ENABLE)<<16)|PCB_SELECTED_ENABLE));
#endif
    printf("{\"axis\":%d,\"stage\":%d,\"CR1\":%lu,\"CCMR1\":%lu,\"CCMR2\":%lu,\"CCER\":%lu,\"init_BDTR\":%lu,\"DIER\":%lu,\"AF1\":%lu,\"AF2\":%lu}\n",
           PCB_TEST_AXIS,PCB_STAGE,(unsigned long)t->CR1,(unsigned long)t->CCMR1,
           (unsigned long)t->CCMR2,(unsigned long)t->CCER,(unsigned long)init_bdtr,
           (unsigned long)t->DIER,(unsigned long)t->AF1,(unsigned long)t->AF2);
    board_inhibit();assert((t->BDTR&TIM_BDTR_MOE)==0 && !bridge_authorized);
    return 0;
}
