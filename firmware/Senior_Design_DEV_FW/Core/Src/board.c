/* Hand-owned NUCLEO-H753ZI BSP. See docs/BOARD_CONFIG.md before changing pins. */
#include "board.h"
#include "demo_config.h"
#include "pwm_scope.h"
#include <string.h>
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart3;
TIM_HandleTypeDef htim1, htim7;
static IWDG_HandleTypeDef watchdog;
static volatile motor_fault_t latched_fault;
static volatile bool bridge_authorized;
static volatile bool stop_latched=true;
static volatile uint32_t encoder_ms, health_ms, adc_ms;
static volatile uint16_t encoder_raw, adc_raw;
static volatile bool encoder_valid, magnet_good, adc_valid;
static uint8_t encoder_data[2], health_data;
static bool read_health;
#if !DEMO_PWM_SCOPE_TEST && !DEMO_BOARD_OPEN_ACTIVE
static uint32_t health_due;
#endif
uint32_t board_lock(void) { uint32_t m = __get_PRIMASK(); __disable_irq(); return m; }
void board_unlock(uint32_t m) { __set_PRIMASK(m); }
uint32_t board_us(void) { return TIM2->CNT; }
void board_inhibit(void) {
    uint32_t mask = board_lock();
    bridge_authorized = false;
    if (__HAL_RCC_TIM1_IS_CLK_ENABLED()) {
        CLEAR_BIT(TIM1->DIER, TIM_DIER_BIE); CLEAR_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    }
    if (__HAL_RCC_GPIOG_IS_CLK_ENABLED()) GPIOG->BSRR = (uint32_t)GPIO_PIN_4 << 16;
    __DSB(); board_unlock(mask);
}
void board_trip(motor_fault_t fault) {
    uint32_t mask = board_lock();
    if (latched_fault == FAULT_NONE) latched_fault = fault;
    board_inhibit(); board_unlock(mask);
}
void board_clear_trip(void) { uint32_t m=board_lock(); latched_fault=FAULT_NONE; board_unlock(m); }
void board_kill(void) { uint32_t m=board_lock(); stop_latched=true; board_inhibit(); board_unlock(m); }
void board_release_stop(void) { stop_latched=false; }
motor_fault_t board_fault(void) { return latched_fault; }
static void check(HAL_StatusTypeDef s) { if (s != HAL_OK) Error_Handler(); }
static void clock_init(void) {
    RCC_OscInitTypeDef o = {0}; RCC_ClkInitTypeDef c = {0};
    RCC_PeriphCLKInitTypeDef p = {0};
    check(HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY));
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    uint32_t start=HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) if (HAL_GetTick()-start>100u) Error_Handler();
    o.OscillatorType=RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
    o.HSIState=RCC_HSI_DIV1; o.HSICalibrationValue=RCC_HSICALIBRATION_DEFAULT;
    o.LSIState=RCC_LSI_ON;
    o.PLL.PLLState=RCC_PLL_ON; o.PLL.PLLSource=RCC_PLLSOURCE_HSI;
    o.PLL.PLLM=16; o.PLL.PLLN=100; o.PLL.PLLP=2; o.PLL.PLLQ=4; o.PLL.PLLR=2;
    o.PLL.PLLRGE=RCC_PLL1VCIRANGE_2; o.PLL.PLLVCOSEL=RCC_PLL1VCOWIDE;
    check(HAL_RCC_OscConfig(&o));
    c.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|
                RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_D1PCLK1|RCC_CLOCKTYPE_D3PCLK1;
    c.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; c.SYSCLKDivider=RCC_SYSCLK_DIV1;
    c.AHBCLKDivider=RCC_HCLK_DIV2; c.APB1CLKDivider=RCC_APB1_DIV2;
    c.APB2CLKDivider=RCC_APB2_DIV2; c.APB3CLKDivider=RCC_APB3_DIV2; c.APB4CLKDivider=RCC_APB4_DIV2;
    check(HAL_RCC_ClockConfig(&c,FLASH_LATENCY_4));
    p.PeriphClockSelection=RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_I2C1|RCC_PERIPHCLK_USART3;
    p.PLL2.PLL2M=16; p.PLL2.PLL2N=50; p.PLL2.PLL2P=4; p.PLL2.PLL2Q=4; p.PLL2.PLL2R=4;
    p.PLL2.PLL2RGE=RCC_PLL2VCIRANGE_2; p.PLL2.PLL2VCOSEL=RCC_PLL2VCOWIDE;
    p.AdcClockSelection=RCC_ADCCLKSOURCE_PLL2;
    p.I2c123ClockSelection=RCC_I2C123CLKSOURCE_D2PCLK1;
    p.Usart234578ClockSelection=RCC_USART234578CLKSOURCE_D2PCLK1;
    check(HAL_RCCEx_PeriphCLKConfig(&p));
    if (HAL_RCC_GetSysClockFreq()!=200000000u || HAL_RCC_GetHCLKFreq()!=100000000u)
        Error_Handler();
}
static void gpio(GPIO_TypeDef *port,uint32_t pins,uint32_t mode,uint32_t pull,uint32_t af) {
    GPIO_InitTypeDef g={0}; g.Pin=pins; g.Mode=mode; g.Pull=pull;
    g.Speed=GPIO_SPEED_FREQ_HIGH; g.Alternate=af; HAL_GPIO_Init(port,&g);
}
static void pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIOG->BSRR=(uint32_t)GPIO_PIN_4<<16;
    gpio(GPIOG,GPIO_PIN_4,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    gpio(GPIOE,GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13,
         GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
    gpio(GPIOB,GPIO_PIN_0,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
    /* PE6 is TIM1_BKIN2 on AF1, active LOW in BDTR. */
    gpio(GPIOE,GPIO_PIN_6,GPIO_MODE_AF_PP,GPIO_PULLUP,GPIO_AF1_TIM1);
    gpio(GPIOB,GPIO_PIN_6|GPIO_PIN_7,GPIO_MODE_AF_OD,GPIO_NOPULL,GPIO_AF4_I2C1);
    gpio(GPIOD,GPIO_PIN_8|GPIO_PIN_9,GPIO_MODE_AF_PP,GPIO_PULLUP,GPIO_AF7_USART3);
    gpio(GPIOA,GPIO_PIN_3,GPIO_MODE_ANALOG,GPIO_NOPULL,0);
    gpio(GPIOB,GPIO_PIN_14,GPIO_MODE_OUTPUT_PP,GPIO_NOPULL,0); /* red LD3 */
#if DEMO_BOARD_OPEN_ACTIVE
    gpio(GPIOG,GPIO_PIN_2,GPIO_MODE_INPUT,GPIO_PULLDOWN,0); /* Physical RUN, CN11 pin 42. */
    GPIOE->BSRR=(uint32_t)GPIO_PIN_1<<16;
    gpio(GPIOE,GPIO_PIN_1,GPIO_MODE_OUTPUT_PP,GPIO_NOPULL,0); /* yellow LD2: armed. */
#endif
}
static void pwm_init(void) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    htim1.Instance=TIM1; htim1.Init.Prescaler=0;
    htim1.Init.CounterMode=TIM_COUNTERMODE_CENTERALIGNED1; htim1.Init.Period=DEMO_PWM_ARR;
    htim1.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1; htim1.Init.RepetitionCounter=0;
    htim1.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    check(HAL_TIM_PWM_Init(&htim1));
    TIM_OC_InitTypeDef oc={0}; oc.OCMode=TIM_OCMODE_PWM1; oc.Pulse=1250;
    oc.OCPolarity=TIM_OCPOLARITY_HIGH; oc.OCNPolarity=TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState=TIM_OCIDLESTATE_RESET; oc.OCNIdleState=TIM_OCNIDLESTATE_RESET;
    check(HAL_TIM_PWM_ConfigChannel(&htim1,&oc,TIM_CHANNEL_1));
    check(HAL_TIM_PWM_ConfigChannel(&htim1,&oc,TIM_CHANNEL_2));
    check(HAL_TIM_PWM_ConfigChannel(&htim1,&oc,TIM_CHANNEL_3));
    TIM_BreakDeadTimeConfigTypeDef b={0};
    b.OffStateRunMode=TIM_OSSR_ENABLE; b.OffStateIDLEMode=TIM_OSSI_ENABLE;
    b.LockLevel=TIM_LOCKLEVEL_1; b.DeadTime=50; /* 500 ns at 100 MHz tDTS */
    b.BreakState=TIM_BREAK_DISABLE; b.BreakPolarity=TIM_BREAKPOLARITY_HIGH;
    b.Break2State=TIM_BREAK2_ENABLE; b.Break2Polarity=TIM_BREAK2POLARITY_LOW;
    b.Break2Filter=0;
    b.AutomaticOutput=TIM_AUTOMATICOUTPUT_DISABLE;
    TIMEx_BreakInputConfigTypeDef input={0};
    input.Source=TIM_BREAKINPUTSOURCE_BKIN; input.Enable=TIM_BREAKINPUTSOURCE_ENABLE;
    input.Polarity=TIM_BREAKINPUTSOURCE_POLARITY_HIGH;
    check(HAL_TIMEx_ConfigBreakInput(&htim1,TIM_BREAKINPUT_BRK2,&input));
    check(HAL_TIMEx_ConfigBreakDeadTime(&htim1,&b));
    /* HAL PWM_Start would assert MOE. Start the counter/channels explicitly OFF. */
    SET_BIT(TIM1->CCER,TIM_CCER_CC1E|TIM_CCER_CC1NE|TIM_CCER_CC2E|
             TIM_CCER_CC2NE|TIM_CCER_CC3E|TIM_CCER_CC3NE);
    TIM1->EGR=TIM_EGR_UG; CLEAR_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    SET_BIT(TIM1->CR1,TIM_CR1_CEN);
    HAL_NVIC_SetPriority(TIM1_BRK_IRQn,0,0); HAL_NVIC_EnableIRQ(TIM1_BRK_IRQn);
}
static void sensors_init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE(); hi2c1.Instance=I2C1;
    hi2c1.Init.Timing=0x40932F35u; /* ~100 kHz, 50 MHz kernel; scope on real harness */
    hi2c1.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE; hi2c1.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    check(HAL_I2C_Init(&hi2c1)); check(HAL_I2CEx_ConfigAnalogFilter(&hi2c1,I2C_ANALOGFILTER_ENABLE));
    check(HAL_I2CEx_ConfigDigitalFilter(&hi2c1,0));
    HAL_NVIC_SetPriority(I2C1_EV_IRQn,3,0); HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn,3,0); HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    __HAL_RCC_ADC12_CLK_ENABLE(); hadc1.Instance=ADC1;
    hadc1.Init.ClockPrescaler=ADC_CLOCK_ASYNC_DIV4; hadc1.Init.Resolution=ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode=ADC_SCAN_DISABLE; hadc1.Init.EOCSelection=ADC_EOC_SINGLE_CONV;
    hadc1.Init.NbrOfConversion=1; hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge=ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ConversionDataManagement=ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun=ADC_OVR_DATA_OVERWRITTEN;
    check(HAL_ADC_Init(&hadc1));
    ADC_MultiModeTypeDef multi={0}; multi.Mode=ADC_MODE_INDEPENDENT;
    check(HAL_ADCEx_MultiModeConfigChannel(&hadc1,&multi));
    ADC_ChannelConfTypeDef ch={0}; ch.Channel=ADC_CHANNEL_15; ch.Rank=ADC_REGULAR_RANK_1;
    ch.SamplingTime=ADC_SAMPLETIME_64CYCLES_5; ch.SingleDiff=ADC_SINGLE_ENDED; ch.OffsetNumber=ADC_OFFSET_NONE;
    check(HAL_ADC_ConfigChannel(&hadc1,&ch));
    check(HAL_ADCEx_Calibration_Start(&hadc1,ADC_CALIB_OFFSET_LINEARITY,ADC_SINGLE_ENDED));
    HAL_NVIC_SetPriority(ADC_IRQn,3,0); HAL_NVIC_EnableIRQ(ADC_IRQn);
}
static void uart_init(void) {
    __HAL_RCC_USART3_CLK_ENABLE(); huart3.Instance=USART3;
    huart3.Init.BaudRate=115200; huart3.Init.WordLength=UART_WORDLENGTH_8B;
    huart3.Init.StopBits=UART_STOPBITS_1; huart3.Init.Parity=UART_PARITY_NONE;
    huart3.Init.Mode=UART_MODE_TX_RX; huart3.Init.HwFlowCtl=UART_HWCONTROL_NONE;
    huart3.Init.OverSampling=UART_OVERSAMPLING_16; huart3.Init.OneBitSampling=UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.Init.ClockPrescaler=UART_PRESCALER_DIV1; check(HAL_UART_Init(&huart3));
    check(HAL_UARTEx_SetTxFifoThreshold(&huart3,UART_TXFIFO_THRESHOLD_1_8));
    check(HAL_UARTEx_SetRxFifoThreshold(&huart3,UART_RXFIFO_THRESHOLD_1_8));
    check(HAL_UARTEx_DisableFifoMode(&huart3));
    HAL_NVIC_SetPriority(USART3_IRQn,1,0); HAL_NVIC_EnableIRQ(USART3_IRQn);
}
void board_init(void) {
    /* D-cache intentionally disabled. ETH DMA has its own D2 linker region. */
    SCB_EnableICache(); HAL_Init(); clock_init(); pins_init(); pwm_init(); sensors_init(); uart_init();
    __HAL_RCC_TIM2_CLK_ENABLE(); TIM2->PSC=99; TIM2->ARR=0xffffffffu;
    TIM2->EGR=TIM_EGR_UG; TIM2->CR1=TIM_CR1_CEN;
    __HAL_RCC_TIM7_CLK_ENABLE(); htim7.Instance=TIM7;
    htim7.Init.Prescaler=99; htim7.Init.Period=999; htim7.Init.CounterMode=TIM_COUNTERMODE_UP;
    check(HAL_TIM_Base_Init(&htim7));
    HAL_NVIC_SetPriority(TIM7_IRQn,2,0); HAL_NVIC_EnableIRQ(TIM7_IRQn);
    HAL_NVIC_SetPriority(SysTick_IRQn,4,0);
    board_inhibit();
}
void board_start_control(void) { check(HAL_TIM_Base_Start_IT(&htim7)); }
void board_sensors_start(void) {
    /* Volatile CONF only: no OTP burn. Fastest slow filter (SF=11), no hysteresis. */
    uint8_t config[2]={0x03,0x00}, verify[2]={0};
    if (HAL_I2C_Mem_Write(&hi2c1,0x36u<<1,0x07,I2C_MEMADD_SIZE_8BIT,config,2,20)!=HAL_OK ||
        HAL_I2C_Mem_Read(&hi2c1,0x36u<<1,0x07,I2C_MEMADD_SIZE_8BIT,verify,2,20)!=HAL_OK ||
        memcmp(config,verify,2)!=0) encoder_valid=false;
    /* Boot completes inhibited even with the encoder absent; readiness stays false. */
    else encoder_valid=true;
}
void board_sensor_tick(void) {
#if !DEMO_PWM_SCOPE_TEST && !DEMO_BOARD_OPEN_ACTIVE
    if (HAL_I2C_GetState(&hi2c1)==HAL_I2C_STATE_READY) {
        uint32_t now=HAL_GetTick(); read_health=(int32_t)(now-health_due)>=0;
        HAL_StatusTypeDef s=HAL_I2C_Mem_Read_IT(&hi2c1,0x36u<<1,
            read_health?0x0bu:0x0cu,I2C_MEMADD_SIZE_8BIT,
            read_health?&health_data:encoder_data,read_health?1:2);
        if (s==HAL_OK && read_health) health_due=now+20u;
    }
#endif
    if ((HAL_ADC_GetState(&hadc1)&HAL_ADC_STATE_REG_BUSY)==0u)
        (void)HAL_ADC_Start_IT(&hadc1);
}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h) {
    if (h!=&hi2c1) return;
    uint32_t mask=board_lock();
    if (read_health) { magnet_good=(health_data&0x38u)==0x20u; health_ms=HAL_GetTick(); }
    else { encoder_raw=((uint16_t)(encoder_data[0]&0x0fu)<<8)|encoder_data[1]; encoder_ms=HAL_GetTick(); }
    board_unlock(mask);
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h) {
    if (h==&hi2c1) { uint32_t m=board_lock(); magnet_good=false; board_unlock(m); }
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) {
    if (h!=&hadc1) return;
    uint32_t m=board_lock(); adc_raw=(uint16_t)HAL_ADC_GetValue(h); adc_ms=HAL_GetTick();
    adc_valid=true; board_unlock(m);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h) { if (h==&hadc1) adc_valid=false; }
void board_inputs(motor_input_t *in,uint16_t *adc) {
    uint32_t m=board_lock();
    in->raw_angle=encoder_raw; in->encoder_ms=encoder_ms; in->health_ms=health_ms;
    in->encoder_valid=encoder_valid && encoder_ms!=0; in->magnet_good=magnet_good;
    in->adc_valid=adc_valid; in->adc_ms=adc_ms; *adc=adc_raw;
    in->driver_good=(GPIOE->IDR&GPIO_PIN_6)!=0;
    in->external_fault=latched_fault; in->hard_fault=latched_fault!=FAULT_NONE;
    board_unlock(m);
}
bool board_run_high(void) { return (GPIOG->IDR&GPIO_PIN_2)!=0; }
void board_apply(const motor_output_t *out) {
#if DEMO_PWM_SCOPE_TEST
    (void)out; board_inhibit(); /* This build cannot authorize driver ENABLE. */
#else
    uint32_t mask=board_lock();
#if DEMO_BOARD_OPEN_ACTIVE
    if (!board_run_high()) { board_kill(); board_unlock(mask); return; }
#endif
    if (!out->enable || latched_fault!=FAULT_NONE || stop_latched) { board_inhibit(); board_unlock(mask); return; }
    GPIOG->BSRR=GPIO_PIN_4;
    if (!out->pwm) {
        bridge_authorized=false; CLEAR_BIT(TIM1->DIER,TIM_DIER_BIE); CLEAR_BIT(TIM1->BDTR,TIM_BDTR_MOE);
    } else if ((GPIOE->IDR&GPIO_PIN_6)!=0) {
        /* All three preloads commit together at a natural carrier update. */
        SET_BIT(TIM1->CR1,TIM_CR1_UDIS);
        TIM1->CCR1=(uint32_t)(out->duty[0]*DEMO_PWM_ARR);
        TIM1->CCR2=(uint32_t)(out->duty[1]*DEMO_PWM_ARR);
        TIM1->CCR3=(uint32_t)(out->duty[2]*DEMO_PWM_ARR);
        CLEAR_BIT(TIM1->CR1,TIM_CR1_UDIS);
        if (!bridge_authorized) {
            __HAL_TIM_CLEAR_FLAG(&htim1,TIM_FLAG_BREAK|TIM_FLAG_BREAK2);
            HAL_NVIC_ClearPendingIRQ(TIM1_BRK_IRQn);
            bridge_authorized=true; SET_BIT(TIM1->DIER,TIM_DIER_BIE);
            __DSB(); SET_BIT(TIM1->BDTR,TIM_BDTR_MOE);
        }
    } else board_trip(FAULT_DRIVER);
    board_unlock(mask);
#endif
}
void TIM1_BRK_IRQHandler(void) {
    if ((TIM1->SR&(TIM_SR_BIF|TIM_SR_B2IF))!=0) {
        board_trip(FAULT_DRIVER); __HAL_TIM_CLEAR_FLAG(&htim1,TIM_FLAG_BREAK|TIM_FLAG_BREAK2);
    }
}

void board_pwm_scope_poll(void) {
#if DEMO_PWM_SCOPE_TEST
    uint32_t mask=board_lock();
    /* No path in this profile raises ENABLE, even when MOE is set. */
    GPIOG->BSRR=(uint32_t)GPIO_PIN_4<<16;
    bool permit=(GPIOE->IDR&GPIO_PIN_6)!=0 && latched_fault==FAULT_NONE;
    uint32_t compare=pwm_scope_compare(adc_raw,adc_valid,HAL_GetTick()-adc_ms,permit);
    if (!compare) {
        board_inhibit();
    } else {
        /* Break hardware remains active; BIE is intentionally OFF in scope mode.
         * PE6 low stops pulses; releasing it high allows this test to resume. */
        CLEAR_BIT(TIM1->DIER,TIM_DIER_BIE);
        SET_BIT(TIM1->CR1,TIM_CR1_UDIS);
        TIM1->CCR1=compare; TIM1->CCR2=compare; TIM1->CCR3=compare;
        CLEAR_BIT(TIM1->CR1,TIM_CR1_UDIS);
        if ((TIM1->BDTR&TIM_BDTR_MOE)==0) {
            /* Load the desired compare before the first visible pulse. */
            TIM1->EGR=TIM_EGR_UG;
            __HAL_TIM_CLEAR_FLAG(&htim1,TIM_FLAG_BREAK|TIM_FLAG_BREAK2);
            __DSB(); SET_BIT(TIM1->BDTR,TIM_BDTR_MOE);
        }
    }
    board_unlock(mask);
    board_sensor_tick(); /* ADC only in this profile; no encoder dependency. */
#endif
}
void board_watchdog_start(void) {
    watchdog.Instance=IWDG1; watchdog.Init.Prescaler=IWDG_PRESCALER_32;
    watchdog.Init.Reload=999; watchdog.Init.Window=IWDG_WINDOW_DISABLE; check(HAL_IWDG_Init(&watchdog));
}
void board_watchdog_feed(void) { (void)HAL_IWDG_Refresh(&watchdog); }
void Error_Handler(void) { board_trip(FAULT_INTERNAL); __disable_irq(); for (;;) {} }
