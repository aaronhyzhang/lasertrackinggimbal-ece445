#include <stdlib.h>
/* Unrelated peripheral services are deliberately not simulated. A test that
 * reaches one aborts rather than silently treating a stub as real hardware. */
#include "board.h"
#include "drv8304.h"
_Noreturn void unexpected_call(void);
HAL_StatusTypeDef       HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc, uint32_t CalibrationMode, uint32_t SingleDiff) { unexpected_call(); }
HAL_StatusTypeDef       HAL_ADCEx_MultiModeConfigChannel(ADC_HandleTypeDef *hadc, ADC_MultiModeTypeDef *multimode) { unexpected_call(); }
HAL_StatusTypeDef       HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *sConfig) { unexpected_call(); }
uint32_t                HAL_ADC_GetState(const ADC_HandleTypeDef *hadc) { unexpected_call(); }
uint32_t                HAL_ADC_GetValue(const ADC_HandleTypeDef *hadc) { unexpected_call(); }
HAL_StatusTypeDef       HAL_ADC_Init(ADC_HandleTypeDef *hadc) { unexpected_call(); }
HAL_StatusTypeDef       HAL_ADC_Start_IT(ADC_HandleTypeDef *hadc) { unexpected_call(); }
HAL_StatusTypeDef     HAL_IWDG_Init(IWDG_HandleTypeDef *hiwdg) { unexpected_call(); }
HAL_StatusTypeDef     HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg) { unexpected_call(); }
HAL_StatusTypeDef HAL_Init(void) { unexpected_call(); }
HAL_StatusTypeDef HAL_PWREx_ConfigSupply                  (uint32_t SupplySource) { unexpected_call(); }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef  *PeriphClkInit) { unexpected_call(); }
HAL_StatusTypeDef HAL_RCC_ClockConfig(const RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency) { unexpected_call(); }
uint32_t HAL_RCC_GetHCLKFreq(void) { unexpected_call(); }
uint32_t HAL_RCC_GetSysClockFreq(void) { unexpected_call(); }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct) { unexpected_call(); }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi) { unexpected_call(); }
void HAL_SYSCFG_AnalogSwitchConfig(uint32_t SYSCFG_AnalogSwitch , uint32_t SYSCFG_SwitchState ) { unexpected_call(); }

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h,const uint8_t *tx,uint8_t *rx,uint16_t n,uint32_t timeout) { unexpected_call(); }
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *h,uint32_t s,uint32_t d,uint32_t n) { unexpected_call(); }
HAL_StatusTypeDef HAL_DMA_Abort_IT(DMA_HandleTypeDef *h) { unexpected_call(); }
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *h) { unexpected_call(); }
void app_control_tick(void) { unexpected_call(); }
_Noreturn void unexpected_call(void) { abort(); }
