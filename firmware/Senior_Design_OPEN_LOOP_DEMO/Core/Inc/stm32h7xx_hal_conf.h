/* HAL configuration for the H753 open-loop demo only. */
#ifndef STM32H7xx_HAL_CONF_H
#define STM32H7xx_HAL_CONF_H
#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HSE_VALUE 25000000UL
#define HSE_STARTUP_TIMEOUT 100UL
#define CSI_VALUE 4000000UL
#define HSI_VALUE 64000000UL
#define LSI_VALUE 32000UL
#define LSE_VALUE 32768UL
#define LSE_STARTUP_TIMEOUT 5000UL
#define EXTERNAL_CLOCK_VALUE 12288000UL
#define VDD_VALUE 3300UL
#define TICK_INT_PRIORITY 15UL
#define USE_RTOS 0U
#define USE_HAL_ADC_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U
#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
/* ADC/TIM handle definitions require DMA types; no DMA transfer is used. */
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_adc.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_iwdg.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_tim.h"
#define assert_param(expr) ((void)0U)
#endif
