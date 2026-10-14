#include "board.h"
#include "demo_app.h"
void NMI_Handler(void) { board_trip(FAULT_INTERNAL); for (;;) {} }
void HardFault_Handler(void) { board_trip(FAULT_INTERNAL); for (;;) {} }
void MemManage_Handler(void) { board_trip(FAULT_INTERNAL); for (;;) {} }
void BusFault_Handler(void) { board_trip(FAULT_INTERNAL); for (;;) {} }
void UsageFault_Handler(void) { board_trip(FAULT_INTERNAL); for (;;) {} }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { HAL_IncTick(); }
void TIM7_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim7,TIM_FLAG_UPDATE) && __HAL_TIM_GET_IT_SOURCE(&htim7,TIM_IT_UPDATE)) {
        __HAL_TIM_CLEAR_IT(&htim7,TIM_IT_UPDATE); app_control_tick();
    }
}
void ADC_IRQHandler(void) { HAL_ADC_IRQHandler(&hadc1); }
