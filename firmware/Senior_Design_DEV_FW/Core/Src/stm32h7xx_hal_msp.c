/* Peripheral MSP ownership is in board.c and demo_network.c. */
#include "stm32h7xx_hal.h"
void HAL_MspInit(void) { __HAL_RCC_SYSCFG_CLK_ENABLE(); }
