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
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, const uint8_t *pTxData, uint8_t *pRxData,
                                          uint16_t Size, uint32_t Timeout) { unexpected_call(); }
void HAL_SYSCFG_AnalogSwitchConfig(uint32_t SYSCFG_AnalogSwitch , uint32_t SYSCFG_SwitchState ) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *htim,
                                                const TIM_BreakDeadTimeConfigTypeDef *sBreakDeadTimeConfig) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakInput(TIM_HandleTypeDef *htim, uint32_t BreakInput,
                                             const TIMEx_BreakInputConfigTypeDef *sBreakInputConfig) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, const TIM_OC_InitTypeDef *sConfig,
                                            uint32_t Channel) { unexpected_call(); }
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim) { unexpected_call(); }
bool drv8304_configure(drv8304_transfer_t transfer, void *context,
                       drv8304_snapshot_t *snapshot) { unexpected_call(); }
bool drv8304_verify(drv8304_transfer_t transfer, void *context,
                    drv8304_snapshot_t *snapshot) { unexpected_call(); }
