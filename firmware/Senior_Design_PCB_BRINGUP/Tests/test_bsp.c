/* Windows x64 register audit of actual board.c and vendor GPIO HAL.
 * Fixed STM32 address regions are reserved as private process RAM, never hardware.
 * NVIC/CPU intrinsics and W1C semantics are simulated. Unrelated HAL calls abort.
 * This does not execute the real timer clock/init or establish electrical timing. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "demo_app.h"
#ifdef NDEBUG
#error Audit assertions must be enabled
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
static uint32_t test_ms;
uint32_t HAL_GetTick(void) { return test_ms; }
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t priority,uint32_t sub) {
    (void)irq;(void)priority;(void)sub;
}
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq) { (void)irq; }
void app_stop(void) { board_kill(); }
/* Unused peripheral services resolve here; executing one aborts the audit. */
_Noreturn void unexpected_call(void) { fputs("FAIL: unexpected peripheral service\n",stderr); abort(); }

static void map(uintptr_t address,size_t bytes) {
    void *mapped=VirtualAlloc((void *)address,bytes,0x3000,4);
    if (mapped!=(void *)address) abort();
}
static unsigned mode(GPIO_TypeDef *gpio,unsigned pin) { return (gpio->MODER>>(pin*2))&3u; }
static unsigned af(GPIO_TypeDef *gpio,unsigned pin) { return (gpio->AFR[pin/8]>>((pin%8)*4))&15u; }
static void live(void) {
    driver_trip_snapshot_t empty={0};g_driver_trip=empty;
    latched_fault=FAULT_NONE;stop_latched=false;bridge_authorized=false;
    wake_requested=true;driver_ready=true;wake_ms=0;test_ms=21;
    GPIOD->IDR=PCB_STOP;GPIOE->IDR=PCB_NFAULT;EXTI->PR1=0;
    TIM1->BDTR=TIM8->BDTR=0;TIM1->DIER=TIM8->DIER=0;
}
int main(void) {
    map(0x58000000u,0x30000);map(0x40000000u,0x20000);
    pins_init();
    assert(mode(GPIOA,9)==1 && mode(GPIOG,7)==1 && mode(GPIOG,4)==1);
    assert(mode(GPIOG,5)==1 && mode(GPIOG,6)==1);
    assert(mode(GPIOE,6)==0); /* nFAULT remains an input, never push-pull. */
    assert(((SYSCFG->EXTICR[1]>>8)&15u)==4u); /* EXTI6 -> PE6, not PC6. */
    assert((EXTI->FTSR1&PCB_NFAULT)!=0 && (EXTI->RTSR1&PCB_NFAULT)==0);
    assert((EXTI_D1->IMR1&PCB_NFAULT)!=0);
    assert(mode(GPIOC,6)==2 && af(GPIOC,6)==3);
    assert(mode(GPIOC,7)==2 && af(GPIOC,7)==3);
    assert(mode(GPIOC,8)==2 && af(GPIOC,8)==3);
    assert(mode(GPIOA,5)==2 && af(GPIOA,5)==3);
    assert(mode(GPIOB,14)==2 && af(GPIOB,14)==3);
    assert(mode(GPIOB,15)==2 && af(GPIOB,15)==3);
    assert(mode(GPIOE,8)==1 && mode(GPIOE,9)==1 && mode(GPIOE,11)==1);
    assert(mode(GPIOE,12)==1 && mode(GPIOE,13)==1 && mode(GPIOB,0)==1);
    __HAL_RCC_TIM8_CLK_ENABLE();
    motor_output_t out={.enable=true,.pwm=true,.duty={.5f,.5f,.5f}};
    live();
    EXTI->PR1=PCB_NFAULT; /* Normal wake edge before PWM authorization. */
    EXTI9_5_IRQHandler();
    assert(latched_fault==FAULT_NONE && !bridge_authorized && EXTI->PR1==0);
    EXTI->PR1=PCB_NFAULT; /* A stale wake pending bit is cleared on start. */
    board_apply(&out);
    assert(bridge_authorized && (TIM8->BDTR&TIM_BDTR_MOE)!=0 && EXTI->PR1==0);
    EXTI9_5_IRQHandler(); /* Empty/stale NVIC entry cannot create a fault. */
    assert(latched_fault==FAULT_NONE && bridge_authorized);
    assert(TIM8->CCR1==1250 && TIM8->CCR2==1250 && TIM8->CCR3==1250);
    assert(GPIOG->BSRR==((uint32_t)GPIO_PIN_4<<16|GPIO_PIN_7));
    EXTI->PR1=PCB_NFAULT;GPIOE->IDR=0; /* Falling nFAULT during authorized PWM. */
    EXTI9_5_IRQHandler();
    assert(latched_fault==FAULT_DRIVER && !bridge_authorized && !driver_ready);
    assert(g_driver_trip.source==DRIVER_TRIP_NFAULT_EDGE);
    assert(g_driver_trip.wake_age_ms==21 && !g_driver_trip.nfault_high);
    assert(g_driver_trip.driver_ready && g_driver_trip.pwm_authorized);
    assert((TIM8->BDTR&TIM_BDTR_MOE)==0 && GPIOG->BSRR==(uint32_t)PCB_ENABLES<<16);
    GPIOE->IDR=PCB_NFAULT;board_apply(&out); /* Recovery cannot restart a latched fault. */
    assert((TIM8->BDTR&TIM_BDTR_MOE)==0 && !bridge_authorized);
    assert(g_driver_trip.source==DRIVER_TRIP_NFAULT_EDGE && !g_driver_trip.nfault_high);
    live();GPIOE->IDR=0;board_apply(&out); /* Low live level before first PWM. */
    assert(latched_fault==FAULT_DRIVER && (TIM8->BDTR&TIM_BDTR_MOE)==0);
    assert(g_driver_trip.source==DRIVER_TRIP_PWM_PERMISSION);
    assert(!g_driver_trip.nfault_high && g_driver_trip.driver_ready && !g_driver_trip.pwm_authorized);
    live();driver_ready=false;board_apply(&out);
    assert(g_driver_trip.source==DRIVER_TRIP_PWM_PERMISSION);
    assert(g_driver_trip.nfault_high && !g_driver_trip.driver_ready);
    live();board_apply(&out);EXTI->PR1=PCB_NFAULT; /* Brief pulse already released. */
    EXTI9_5_IRQHandler();
    assert(g_driver_trip.source==DRIVER_TRIP_NFAULT_EDGE && g_driver_trip.nfault_high);
    assert((TIM8->BDTR&TIM_BDTR_MOE)==0);
    live();board_apply(&out);EXTI->PR1=PCB_STOP;EXTI9_5_IRQHandler();
    assert(stop_latched && !bridge_authorized && (TIM8->BDTR&TIM_BDTR_MOE)==0);
    assert(g_driver_trip.source==DRIVER_TRIP_NONE);
    live();board_apply(&out);EXTI->PR1=PCB_STOP|PCB_NFAULT;EXTI9_5_IRQHandler();
    assert(stop_latched && latched_fault==FAULT_NONE && g_driver_trip.source==DRIVER_TRIP_NONE);
    assert((TIM8->BDTR&TIM_BDTR_MOE)==0);
    puts("PASS: actual J4 BSP/vendor GPIO pin modes, AF3, PE6 EXTI route, wake-edge suppression, first PWM, live-low/edge fault inhibition, first-trip preservation, released-pulse latch, STOP priority.");
}
