/* STM32H743 PCB support: selected driver / VR1 / S1 / S2; one-axis TIM1/TIM8 PWM. */
#include "board.h"
#include "demo_config.h"
#include "pcb_config.h"
#include "pcb_pins.h"
#include "pcb_pwm.h"
#include "drv8304.h"
#include "demo_app.h"
#include <string.h>
static drv8304_snapshot_t sense_driver_status[2];
static bool sense_driver_ready[2];
static const char sense_spi_context[2];
static SPI_HandleTypeDef hspi2;
volatile drv8304_snapshot_t g_driver_status;
volatile driver_trip_snapshot_t g_driver_trip;
volatile driver_fault_capture_t g_driver_fault={
    .state=PCB_BENCH_CAPTURE_DRIVER_FAULT?DRIVER_FAULT_WAITING:DRIVER_FAULT_DISABLED};
static uint32_t fault_capture_started_us;
static const char fault_capture_spi_context;
unsigned board_active_axis=PCB_TEST_AXIS;
void board_select_axis(unsigned axis) { board_active_axis=axis; }
#if CONTROL_DUAL_AXIS
typedef struct { bool wake,ready,authorized;uint32_t wake_time,epoch,verified;TIM_HandleTypeDef timer;drv8304_snapshot_t status; } axis_board_t;
static axis_board_t axes[2];
#define wake_requested axes[board_active_axis].wake
#define driver_ready axes[board_active_axis].ready
#define bridge_authorized axes[board_active_axis].authorized
#define wake_ms axes[board_active_axis].wake_time
#define wake_epoch axes[board_active_axis].epoch
#define last_verify axes[board_active_axis].verified
#define htim_pwm axes[board_active_axis].timer
#else
static volatile bool wake_requested, driver_ready;
static volatile uint32_t wake_ms, wake_epoch;
static uint32_t last_verify;
static TIM_HandleTypeDef htim_pwm;
#endif
TIM_HandleTypeDef htim7;
static IWDG_HandleTypeDef watchdog;
static volatile motor_fault_t latched_fault;
#if !CONTROL_DUAL_AXIS
static volatile bool bridge_authorized;
#endif
static volatile bool stop_latched = true;

uint32_t board_lock(void) { uint32_t m = __get_PRIMASK(); __disable_irq(); return m; }

void board_unlock(uint32_t m) { __set_PRIMASK(m); }

uint32_t board_us(void) { return TIM2->CNT; }

static void fault_capture_cancel(driver_fault_capture_state_t state) {
    if (g_driver_fault.state==DRIVER_FAULT_PENDING) {
        g_driver_fault.elapsed_us=board_us()-fault_capture_started_us;
        g_driver_fault.state=state;
    }
}
/* Called with IRQs masked. STOP and the hardware-timer deadline override capture. */
static bool fault_capture_keep_awake(void) {
    if (!PCB_BENCH_CAPTURE_DRIVER_FAULT || g_driver_fault.state!=DRIVER_FAULT_PENDING) return false;
    if (stop_latched || board_stop_pressed()) fault_capture_cancel(DRIVER_FAULT_STOPPED);
    else if (board_us()-fault_capture_started_us>=DRIVER_FAULT_CAPTURE_US)
        fault_capture_cancel(DRIVER_FAULT_TIMED_OUT);
    return g_driver_fault.state==DRIVER_FAULT_PENDING;
}

void board_inhibit(void) {
    uint32_t mask = board_lock();
    bridge_authorized = false;
#if CONTROL_DUAL_AXIS
    if ((board_active_axis && __HAL_RCC_TIM8_IS_CLK_ENABLED()) || (!board_active_axis && __HAL_RCC_TIM1_IS_CLK_ENABLED()))
        CLEAR_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
#else
    pcb_pwm_stop_all();
#endif
    if (__HAL_RCC_GPIOG_IS_CLK_ENABLED()) {
        if (fault_capture_keep_awake())
            GPIOG->BSRR=(uint32_t)(PCB_ENABLES & ~PCB_SELECTED_ENABLE)<<16;
        else pcb_driver_enable(false);
    }
    if (wake_requested) ++wake_epoch;
    wake_requested=driver_ready=false;
    #if CONTROL_DUAL_AXIS
    sense_driver_ready[board_active_axis]=false;
#else
    sense_driver_ready[0]=sense_driver_ready[1]=false;
#endif
    __DSB(); board_unlock(mask);
}

void board_trip(motor_fault_t fault) {
    uint32_t mask = board_lock();
    if (fault!=FAULT_DRIVER) fault_capture_cancel(DRIVER_FAULT_ABORTED);
    if (latched_fault == FAULT_NONE) latched_fault = fault;
    board_kill(); board_unlock(mask);
}

/* No SPI or waits in the ISR. Optional bench capture preserves an already
 * awake driver briefly, with PWM inhibited, for foreground status reads only. */
static void driver_trip(driver_trip_source_t source) {
    uint32_t mask=board_lock();
    if (latched_fault==FAULT_NONE) {
        driver_trip_snapshot_t snapshot={source,HAL_GetTick()-wake_ms,
            (GPIOE->IDR&PCB_NFAULT)!=0,driver_ready,bridge_authorized};
        if (PCB_BENCH_CAPTURE_DRIVER_FAULT) {
            bridge_authorized=false;
#if CONTROL_DUAL_AXIS
    CLEAR_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
#else
    pcb_pwm_stop_all();
#endif /* Before opening the readback window. */
        }
        if (PCB_BENCH_CAPTURE_DRIVER_FAULT && PCB_STAGE==PCB_MOTOR_RUN &&
            g_driver_fault.state==DRIVER_FAULT_WAITING && wake_requested && driver_ready &&
            !stop_latched && !board_stop_pressed() && (GPIOG->ODR&PCB_SELECTED_ENABLE)!=0) {
            fault_capture_started_us=board_us();
            g_driver_fault.state=DRIVER_FAULT_PENDING;
        }
        board_trip(FAULT_DRIVER);
        g_driver_trip=snapshot; /* PWM off; ENABLE may have the bounded bench readback window. */
    } else board_trip(FAULT_DRIVER);
    board_unlock(mask);
}

void board_kill(void) {
    uint32_t m=board_lock();stop_latched=true;
#if CONTROL_DUAL_AXIS
    unsigned saved=board_active_axis;
    for(unsigned i=0;i<2;i++) { board_select_axis(i);board_inhibit(); }
    board_select_axis(saved);
#else
    board_inhibit();
#endif
    board_unlock(m);
}

void board_release_stop(void) { stop_latched=false; }

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
    p.PeriphClockSelection=RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_SPI2|RCC_PERIPHCLK_I2C1|RCC_PERIPHCLK_I2C2;
    p.I2c123ClockSelection=RCC_I2C123CLKSOURCE_D2PCLK1;
    p.Spi123ClockSelection=RCC_SPI123CLKSOURCE_PLL;
    p.PLL2.PLL2M=16; p.PLL2.PLL2N=50; p.PLL2.PLL2P=4; p.PLL2.PLL2Q=4; p.PLL2.PLL2R=4;
    p.PLL2.PLL2RGE=RCC_PLL2VCIRANGE_2; p.PLL2.PLL2VCOSEL=RCC_PLL2VCOWIDE;
    p.AdcClockSelection=RCC_ADCCLKSOURCE_PLL2;
    check(HAL_RCCEx_PeriphCLKConfig(&p));
    if (HAL_RCC_GetSysClockFreq()!=200000000u || HAL_RCC_GetHCLKFreq()!=100000000u)
        Error_Handler();
}

static void gpio(GPIO_TypeDef *port,uint32_t pins,uint32_t mode,uint32_t pull,uint32_t af) {
    GPIO_InitTypeDef g={0}; g.Pin=pins; g.Mode=mode; g.Pull=pull;
    g.Speed=GPIO_SPEED_FREQ_HIGH; g.Alternate=af; HAL_GPIO_Init(port,&g);
}

static void pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOG_CLK_ENABLE();
    /* Claim both ENABLEs first, before clocks/peripheral initialization. */
    GPIOG->BSRR=(uint32_t)(PCB_ENABLES|PCB_CALS)<<16;
    gpio(GPIOG,PCB_ENABLES|PCB_CALS,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    GPIOE->BSRR=PCB_CS0|((uint32_t)(PCB_PWM0_E|GPIO_PIN_2|GPIO_PIN_3)<<16);
    GPIOA->BSRR=PCB_CS1|((uint32_t)PCB_PWM1_A<<16);
    GPIOB->BSRR=(uint32_t)(PCB_PWM0_B|PCB_PWM1_B)<<16;
    GPIOC->BSRR=(uint32_t)(PCB_PWM1_C|GPIO_PIN_10|GPIO_PIN_11)<<16;
    GPIOD->BSRR=(uint32_t)(PCB_BLUE|PCB_GREEN|PCB_RED)<<16;
    gpio(GPIOE,PCB_CS0,GPIO_MODE_OUTPUT_PP,GPIO_PULLUP,0);
    gpio(GPIOA,PCB_CS1,GPIO_MODE_OUTPUT_PP,GPIO_PULLUP,0);
    gpio(GPIOE,PCB_PWM0_E|GPIO_PIN_2|GPIO_PIN_3,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    gpio(GPIOB,PCB_PWM0_B|PCB_PWM1_B,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    gpio(GPIOA,PCB_PWM1_A,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    gpio(GPIOC,PCB_PWM1_C|GPIO_PIN_10|GPIO_PIN_11,GPIO_MODE_OUTPUT_PP,GPIO_PULLDOWN,0);
    gpio(GPIOD,PCB_BLUE|PCB_GREEN|PCB_RED,GPIO_MODE_OUTPUT_PP,GPIO_NOPULL,0);
    gpio(GPIOD,PCB_ARM,GPIO_MODE_INPUT,GPIO_PULLUP,0);
    gpio(GPIOD,PCB_STOP,GPIO_MODE_IT_FALLING,GPIO_PULLUP,0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn,0,0); HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    gpio(GPIOA,PCB_VR1_PIN|PCB_VR2_PIN,GPIO_MODE_ANALOG,GPIO_NOPULL,0);
    gpio(GPIOE,PCB_NFAULT,GPIO_MODE_INPUT,GPIO_PULLUP,0);
    if (PCB_STAGE==PCB_PWM_CHECK || PCB_STAGE==PCB_MOTOR_RUN) {
#if CONTROL_DUAL_AXIS
        gpio(GPIOE,PCB_PWM0_E,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
        gpio(GPIOB,PCB_PWM0_B,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
        gpio(GPIOA,PCB_PWM1_A,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
        gpio(GPIOB,PCB_PWM1_B,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
        gpio(GPIOC,PCB_PWM1_C,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
#elif PCB_TEST_AXIS == 0
        gpio(GPIOE,PCB_PWM0_E,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
        gpio(GPIOB,PCB_PWM0_B,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF1_TIM1);
#else
        gpio(GPIOA,PCB_PWM1_A,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
        gpio(GPIOB,PCB_PWM1_B,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
        gpio(GPIOC,PCB_PWM1_C,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF3_TIM8);
#endif
    }
    if (!CONTROL_DUAL_AXIS && PCB_TEST_AXIS==0 && (PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_DRIVER_CHECK))
        gpio(GPIOE,PCB_NFAULT,GPIO_MODE_AF_PP,GPIO_PULLUP,GPIO_AF1_TIM1);
    if (CONTROL_DUAL_AXIS || PCB_STAGE==PCB_SENSOR_CHECK || (PCB_TEST_AXIS==1 && (PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_DRIVER_CHECK)))
        gpio(GPIOE,PCB_NFAULT,GPIO_MODE_IT_FALLING,GPIO_PULLUP,0); /* No TIM8 break on PE6. */
}

static void spi_init(void) {
    __HAL_RCC_SPI2_CLK_ENABLE();
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC2,SYSCFG_SWITCH_PC2_CLOSE);
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC3,SYSCFG_SWITCH_PC3_CLOSE);
    gpio(GPIOB,GPIO_PIN_10,GPIO_MODE_AF_PP,GPIO_PULLDOWN,GPIO_AF5_SPI2);
    gpio(GPIOC,GPIO_PIN_2|GPIO_PIN_3,GPIO_MODE_AF_PP,GPIO_NOPULL,GPIO_AF5_SPI2);
    hspi2.Instance=SPI2;
    hspi2.Init.Mode=SPI_MODE_MASTER; hspi2.Init.Direction=SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize=SPI_DATASIZE_16BIT;
    hspi2.Init.CLKPolarity=SPI_POLARITY_LOW; hspi2.Init.CLKPhase=SPI_PHASE_2EDGE;
    hspi2.Init.NSS=SPI_NSS_SOFT; hspi2.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_128;
    hspi2.Init.FirstBit=SPI_FIRSTBIT_MSB; hspi2.Init.TIMode=SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.NSSPMode=SPI_NSS_PULSE_DISABLE;
    hspi2.Init.NSSPolarity=SPI_NSS_POLARITY_LOW;
    hspi2.Init.FifoThreshold=SPI_FIFO_THRESHOLD_01DATA;
    hspi2.Init.MasterKeepIOState=SPI_MASTER_KEEP_IO_STATE_ENABLE;
    check(HAL_SPI_Init(&hspi2)); /* PLL1Q 100 MHz /128 = 781.25 kHz. */
}

static void pwm_init(void) {
#if CONTROL_DUAL_AXIS
    __HAL_RCC_TIM1_CLK_ENABLE();__HAL_RCC_TIM8_CLK_ENABLE();
#elif PCB_TEST_AXIS == 0
    __HAL_RCC_TIM1_CLK_ENABLE();
#else
    __HAL_RCC_TIM8_CLK_ENABLE();
#endif
    htim_pwm.Instance=PCB_PWM_TIMER; htim_pwm.Init.Prescaler=0;
    htim_pwm.Init.CounterMode=TIM_COUNTERMODE_CENTERALIGNED1; htim_pwm.Init.Period=DEMO_PWM_ARR;
    htim_pwm.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1; htim_pwm.Init.RepetitionCounter=0;
    htim_pwm.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    check(HAL_TIM_PWM_Init(&htim_pwm));
    TIM_OC_InitTypeDef oc={0}; oc.OCMode=TIM_OCMODE_PWM1; oc.Pulse=1250;
    oc.OCPolarity=TIM_OCPOLARITY_HIGH; oc.OCNPolarity=TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState=TIM_OCIDLESTATE_RESET; oc.OCNIdleState=TIM_OCNIDLESTATE_RESET;
    check(HAL_TIM_PWM_ConfigChannel(&htim_pwm,&oc,TIM_CHANNEL_1));
    check(HAL_TIM_PWM_ConfigChannel(&htim_pwm,&oc,TIM_CHANNEL_2));
    check(HAL_TIM_PWM_ConfigChannel(&htim_pwm,&oc,TIM_CHANNEL_3));
    /* CH4 has no GPIO: PWM2 OC4REF rises 2 us before the counter peak.
     * One rising edge per 50 us carrier -> ADC1 injected A/B/C sequence. */
    oc.OCMode=TIM_OCMODE_PWM2;oc.Pulse=2300;
    check(HAL_TIM_PWM_ConfigChannel(&htim_pwm,&oc,TIM_CHANNEL_4));
    TIM_MasterConfigTypeDef master={0};
    master.MasterOutputTrigger=TIM_TRGO_RESET;master.MasterOutputTrigger2=TIM_TRGO2_OC4REF;
    master.MasterSlaveMode=TIM_MASTERSLAVEMODE_DISABLE;
    check(HAL_TIMEx_MasterConfigSynchronization(&htim_pwm,&master));
    TIM_BreakDeadTimeConfigTypeDef b={0};
    b.OffStateRunMode=TIM_OSSR_ENABLE; b.OffStateIDLEMode=TIM_OSSI_ENABLE;
    b.LockLevel=TIM_LOCKLEVEL_1; b.DeadTime=50; /* 500 ns at 100 MHz tDTS */
    b.BreakState=TIM_BREAK_DISABLE; b.BreakPolarity=TIM_BREAKPOLARITY_HIGH;
    b.Break2State=(!CONTROL_DUAL_AXIS && PCB_TEST_AXIS==0 && (PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_DRIVER_CHECK))?TIM_BREAK2_ENABLE:TIM_BREAK2_DISABLE; b.Break2Polarity=TIM_BREAK2POLARITY_LOW;
    b.Break2Filter=0;
    b.AutomaticOutput=TIM_AUTOMATICOUTPUT_DISABLE;
    TIMEx_BreakInputConfigTypeDef input={0};
    input.Source=TIM_BREAKINPUTSOURCE_BKIN; input.Enable=TIM_BREAKINPUTSOURCE_ENABLE;
    input.Polarity=TIM_BREAKINPUTSOURCE_POLARITY_HIGH;
    check(HAL_TIMEx_ConfigBreakInput(&htim_pwm,TIM_BREAKINPUT_BRK2,&input));
    check(HAL_TIMEx_ConfigBreakDeadTime(&htim_pwm,&b));
    /* HAL PWM_Start would assert MOE. Start the counter/channels explicitly OFF. */
    SET_BIT(PCB_PWM_TIMER->CCER,TIM_CCER_CC1E|TIM_CCER_CC1NE|TIM_CCER_CC2E|
             TIM_CCER_CC2NE|TIM_CCER_CC3E|TIM_CCER_CC3NE|TIM_CCER_CC4E);
    PCB_PWM_TIMER->EGR=TIM_EGR_UG; CLEAR_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
    SET_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_CEN);
#if PCB_TEST_AXIS == 0
    HAL_NVIC_SetPriority(TIM1_BRK_IRQn,0,0); HAL_NVIC_EnableIRQ(TIM1_BRK_IRQn);
#endif
}


void board_init(void) {
    /* I-cache enabled; D-cache remains disabled. */
    SCB_EnableICache(); HAL_Init(); pins_init(); clock_init();
#if CONTROL_DUAL_AXIS
    board_select_axis(0);pwm_init();board_select_axis(1);pwm_init();
    /* Align both free-running carriers while MOE/ENABLE are off. TIM1 and TIM8
     * share the same APB2 timer clock. Consecutive CEN writes leave only bus-write
     * skew, rather than the entire TIM8 HAL-init delay between PWM phases. */
    CLEAR_BIT(TIM1->CR1,TIM_CR1_CEN);CLEAR_BIT(TIM8->CR1,TIM_CR1_CEN);
    uint32_t cr1a=TIM1->CR1 & ~(TIM_CR1_CEN|TIM_CR1_DIR|TIM_CR1_UDIS);
    uint32_t cr1b=TIM8->CR1 & ~(TIM_CR1_CEN|TIM_CR1_DIR|TIM_CR1_UDIS);
    TIM1->CR1=cr1a & ~TIM_CR1_CMS;TIM8->CR1=cr1b & ~TIM_CR1_CMS;
    TIM1->CNT=0;TIM8->CNT=0;TIM1->EGR=TIM_EGR_UG;TIM8->EGR=TIM_EGR_UG;
    TIM1->CR1=cr1a;TIM8->CR1=cr1b;__DSB();
    TIM1->CR1=cr1a|TIM_CR1_CEN;TIM8->CR1=cr1b|TIM_CR1_CEN;
    board_select_axis(PCB_TEST_AXIS);
#else
    pwm_init();
#endif
    pcb_sense_init();
    if (PCB_STAGE==PCB_DRIVER_CHECK || PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_SENSOR_CHECK) spi_init();
    __HAL_RCC_TIM2_CLK_ENABLE(); TIM2->PSC=99; TIM2->ARR=0xffffffffu;
    TIM2->EGR=TIM_EGR_UG; TIM2->CR1=TIM_CR1_CEN;
    __HAL_RCC_TIM7_CLK_ENABLE(); htim7.Instance=TIM7;
    htim7.Init.Prescaler=99; htim7.Init.Period=999; htim7.Init.CounterMode=TIM_COUNTERMODE_UP;
    check(HAL_TIM_Base_Init(&htim7));
    HAL_NVIC_SetPriority(TIM7_IRQn,2,0); HAL_NVIC_EnableIRQ(TIM7_IRQn);
    HAL_NVIC_SetPriority(SysTick_IRQn,4,0);
    board_kill();
}

void board_start_control(void) { pcb_sense_start();check(HAL_TIM_Base_Start_IT(&htim7)); }

void board_adc_tick(void) { pcb_sense_tick(); }

void board_inputs(motor_input_t *in,uint16_t adc[2]) {
    uint32_t m=board_lock();
    pcb_sense_pots(adc,&in->adc_valid,&in->adc_ms);
    in->driver_good=PCB_STAGE==PCB_PWM_CHECK ||
        (driver_ready && (GPIOE->IDR&PCB_NFAULT)!=0);
    in->current_zero_required=PCB_STAGE==PCB_MOTOR_RUN;
    in->current_zero_ready=driver_ready && pcb_sense_current_zero_ready(CONTROL_DUAL_AXIS?board_active_axis:PCB_TEST_AXIS,wake_epoch);
    in->external_fault=latched_fault; in->hard_fault=latched_fault!=FAULT_NONE;
    board_unlock(m);
}

bool board_arm_pressed(void) { return (GPIOD->IDR&PCB_ARM)==0; }
bool board_stop_pressed(void) { return (GPIOD->IDR&PCB_STOP)==0; }

static void pwm_off(void) {
    bridge_authorized=false;
#if CONTROL_DUAL_AXIS
    CLEAR_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
#else
    pcb_pwm_stop_all();
#endif
}

/* IRQs masked, outputs inhibited. DIR is read-only in center-aligned mode:
 * stop first, temporarily select edge-aligned up-counting, commit preloads,
 * then restore CMS while still stopped. No break flags are cleared here. */
static void pwm_prepare_start(const float duty[3]) {
    CLEAR_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
    CLEAR_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_CEN);
    uint32_t cr1=PCB_PWM_TIMER->CR1 & ~(TIM_CR1_CEN|TIM_CR1_DIR|TIM_CR1_UDIS);
    CLEAR_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_CMS);
    PCB_PWM_TIMER->CR1=cr1 & ~TIM_CR1_CMS;
    pcb_pwm_write(duty);
    PCB_PWM_TIMER->CNT=0;
    PCB_PWM_TIMER->EGR=TIM_EGR_UG; /* UDIS clear: transfer CCR/ARR/PSC preloads. */
    PCB_PWM_TIMER->SR=~TIM_SR_UIF; /* W0C: clear UPDATE only, preserve break flags. */
    PCB_PWM_TIMER->CR1=cr1; /* Restore center-aligned mode, direction initially up. */
    __DSB();
}

void board_apply(const motor_output_t *out) {
    uint32_t mask=board_lock();
    if (board_stop_pressed()) { board_kill(); board_unlock(mask); return; }
    if (PCB_STAGE==PCB_IO_CHECK || !out->enable ||
        latched_fault!=FAULT_NONE || stop_latched) {
        board_inhibit(); board_unlock(mask); return;
    }
    if (PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_DRIVER_CHECK || PCB_STAGE==PCB_SENSOR_CHECK) {
        if (!wake_requested) {
            wake_requested=true; driver_ready=false;
            wake_ms=HAL_GetTick(); ++wake_epoch;
        }
        if (PCB_STAGE==PCB_SENSOR_CHECK) GPIOG->BSRR=PCB_ENABLES; /* GPIO-low gates only. */
        else pcb_driver_enable(true); /* Selected actuator only. */
    } else pcb_driver_enable(false);
    bool drive=out->pwm && (PCB_STAGE==PCB_MOTOR_RUN || PCB_STAGE==PCB_PWM_CHECK);
    if (!drive) pwm_off();
    else if (PCB_STAGE==PCB_PWM_CHECK || (driver_ready && (GPIOE->IDR&PCB_NFAULT)!=0)) {
        if (!CONTROL_DUAL_AXIS && PCB_PWM_SYNC_START && !bridge_authorized) pwm_prepare_start(out->duty);
        else if (!bridge_authorized) pcb_pwm_write(out->duty); /* Fast current ISR owns running duties. */
        if (!bridge_authorized) {
#if !CONTROL_DUAL_AXIS && PCB_TEST_AXIS == 0
            __HAL_TIM_CLEAR_FLAG(&htim_pwm,TIM_FLAG_BREAK|TIM_FLAG_BREAK2);
            HAL_NVIC_ClearPendingIRQ(TIM1_BRK_IRQn);
            if (PCB_STAGE==PCB_MOTOR_RUN) SET_BIT(PCB_PWM_TIMER->DIER,TIM_DIER_BIE);
#else
            /* Ignore only wake/sleep edges recorded before PWM authorization.
             * Recheck the live level after clearing; a later edge remains pending. */
            __HAL_GPIO_EXTI_CLEAR_IT(PCB_NFAULT);
            if (PCB_STAGE==PCB_MOTOR_RUN && (GPIOE->IDR&PCB_NFAULT)==0) {
                driver_trip(DRIVER_TRIP_START_NFAULT_LOW); board_unlock(mask); return;
            }
#endif
            bridge_authorized=true;
            __DSB(); SET_BIT(PCB_PWM_TIMER->BDTR,TIM_BDTR_MOE);
            if (!CONTROL_DUAL_AXIS && PCB_PWM_SYNC_START) SET_BIT(PCB_PWM_TIMER->CR1,TIM_CR1_CEN);
        }
    } else driver_trip(DRIVER_TRIP_PWM_PERMISSION);
    board_unlock(mask);
}

void board_current_pwm(const float duty[3]) {
    uint32_t m=board_lock();
    if (bridge_authorized && !stop_latched && latched_fault==FAULT_NONE && driver_ready &&
        !board_stop_pressed() && (GPIOE->IDR&PCB_NFAULT)!=0) pcb_pwm_write(duty);
    board_unlock(m);
}

static void spi_select_context(void *context,bool selected) {
    if (context==&sense_spi_context[0] || context==&sense_spi_context[1]) {
        GPIOE->BSRR=PCB_CS0;GPIOA->BSRR=PCB_CS1;
        if (selected) {
            if (context==&sense_spi_context[0]) GPIOE->BSRR=(uint32_t)PCB_CS0<<16;
            else GPIOA->BSRR=(uint32_t)PCB_CS1<<16;
        }
    } else pcb_driver_select(selected);
}
static bool spi_transfer(void *context,uint16_t tx,uint16_t *rx) {
    bool capture=context==&fault_capture_spi_context;
    uint32_t mask=board_lock();
    bool allowed=capture ? fault_capture_keep_awake() :
        (wake_requested && !stop_latched && !board_stop_pressed());
    if (capture && !allowed) board_inhibit();
    board_unlock(mask);
    if (!allowed || (capture && tx!=0x8000u && tx!=0x8800u)) return false;
    spi_select_context(context,false); /* Both selects HIGH throughout inactive spacing. */
    uint32_t begin=board_us(); while (board_us()-begin<2u) {}
    spi_select_context(context,true);
    begin=board_us(); while (board_us()-begin<1u) {}
    HAL_StatusTypeDef result=HAL_SPI_TransmitReceive(&hspi2,(uint8_t *)&tx,(uint8_t *)rx,1,2);
    begin=board_us(); while (board_us()-begin<1u) {}
    spi_select_context(context,false);
    if (capture) {
        mask=board_lock();
        allowed=fault_capture_keep_awake();
        if (!allowed) board_inhibit();
        board_unlock(mask);
    }
    return result==HAL_OK && allowed;
}

static void fault_capture_service(void) {
    if (!PCB_BENCH_CAPTURE_DRIVER_FAULT) return;
    uint32_t mask=board_lock();
    if (g_driver_fault.state!=DRIVER_FAULT_PENDING) { board_unlock(mask); return; }
    board_inhibit(); /* MOE stays off; also enforce STOP/deadline before SPI. */
    bool active=g_driver_fault.state==DRIVER_FAULT_PENDING;
    board_unlock(mask);
    if (!active) return;
    drv8304_fault_snapshot_t snapshot={0};
    bool ok=drv8304_read_faults(spi_transfer,(void *)&fault_capture_spi_context,&snapshot);
    mask=board_lock();
    if (g_driver_fault.state==DRIVER_FAULT_PENDING) {
        g_driver_fault.reg[0]=snapshot.reg[0]; g_driver_fault.reg[1]=snapshot.reg[1];
        g_driver_fault.read_mask=snapshot.read_mask;
        g_driver_fault.elapsed_us=board_us()-fault_capture_started_us;
        g_driver_fault.state=ok?DRIVER_FAULT_COMPLETE:DRIVER_FAULT_SPI_ERROR;
    }
    board_inhibit(); /* Success, SPI failure, timeout or STOP all end with EN low. */
    board_unlock(mask);
}

void board_feedback_driver(unsigned axis,sense_driver_t *out) {
    uint32_t mask=board_lock();memset(out,0,sizeof(*out));
    if (axis<2u) {
        uint32_t pin=axis?GPIO_PIN_7:GPIO_PIN_4;
        out->awake=(GPIOG->ODR&pin)!=0;out->epoch=wake_epoch;
#if CONTROL_DUAL_AXIS
        out->epoch=axes[axis].epoch;
#endif
        const volatile drv8304_snapshot_t *snapshot=0;
#if CONTROL_DUAL_AXIS
        out->configured=axes[axis].ready;snapshot=&axes[axis].status;
        out->pwm_active=axes[axis].authorized;
#else
        if (PCB_STAGE==PCB_SENSOR_CHECK) {
            out->configured=sense_driver_ready[axis];snapshot=&sense_driver_status[axis];
        } else if (axis==PCB_TEST_AXIS) {
            out->configured=driver_ready;snapshot=&g_driver_status;
            out->pwm_active=bridge_authorized;
        }
#endif
        if (snapshot) {
            out->csa_register=snapshot->reg[6];out->read_mask=snapshot->read_mask;
            out->failed_address=snapshot->failed_address;
            for (unsigned i=0;i<7;++i) out->reg[i]=snapshot->reg[i];
        }
        static const float gains[]={5,10,20,40};
        out->gain_v_per_v=out->configured?gains[(out->csa_register>>6)&3u]:0;
    }
    board_unlock(mask);
}
static void sense_drivers_service(void) {
    uint32_t mask=board_lock(),epoch=wake_epoch,now=HAL_GetTick();
    bool active=wake_requested && !stop_latched && latched_fault==FAULT_NONE;
    bool ready=driver_ready;uint32_t elapsed=now-wake_ms;board_unlock(mask);
    if (!active || elapsed<DEMO_DRIVER_WAKE_MS || (ready && now-last_verify<100u)) return;
    for (unsigned axis=0;axis<2;++axis) {
        drv8304_snapshot_t snapshot={0};
        bool ok=sense_driver_ready[axis]?
            drv8304_verify(spi_transfer,(void *)&sense_spi_context[axis],&snapshot):
            drv8304_configure(spi_transfer,(void *)&sense_spi_context[axis],&snapshot);
        mask=board_lock();
        if (!wake_requested || wake_epoch!=epoch || stop_latched || latched_fault!=FAULT_NONE) {
            board_unlock(mask);return;
        }
        sense_driver_status[axis]=snapshot;
        if (axis==PCB_TEST_AXIS) g_driver_status=snapshot;
        if (ok && !board_stop_pressed()) sense_driver_ready[axis]=true;
        else { board_trip(FAULT_DRIVER_CONFIG);board_unlock(mask);return; }
        board_unlock(mask);
    }
    mask=board_lock();
    if (wake_requested && wake_epoch==epoch && !stop_latched && latched_fault==FAULT_NONE) {
        if ((GPIOE->IDR&PCB_NFAULT)==0) board_trip(FAULT_DRIVER);
        else { driver_ready=true;last_verify=now; }
    }
    board_unlock(mask);
}
static void board_service_axis(void) {
    if (PCB_STAGE==PCB_SENSOR_CHECK) { sense_drivers_service();return; }
    if (PCB_STAGE!=PCB_MOTOR_RUN && PCB_STAGE!=PCB_DRIVER_CHECK) return;
    fault_capture_service();
    uint32_t mask=board_lock(), epoch=wake_epoch, now=HAL_GetTick();
    bool active=wake_requested && !stop_latched && latched_fault==FAULT_NONE;
    bool ready=driver_ready;
    uint32_t elapsed=now-wake_ms;
    board_unlock(mask);
    if (!active || elapsed<2u || (ready && now-last_verify<100u)) return;
    drv8304_snapshot_t snapshot={0};
    bool ok=ready ? drv8304_verify(spi_transfer,0,&snapshot) :
                    drv8304_configure(spi_transfer,0,&snapshot);
    mask=board_lock();
    /* STOP or a fault can cancel a transaction; it cannot restore permission. */
    if (wake_requested && wake_epoch==epoch && !stop_latched && latched_fault==FAULT_NONE) {
        g_driver_status=snapshot;
#if CONTROL_DUAL_AXIS
        axes[board_active_axis].status=snapshot;
#endif
        if (ok && !board_stop_pressed()) { driver_ready=true; last_verify=now; }
        else board_trip(FAULT_DRIVER_CONFIG);
    }
    board_unlock(mask);
}

void board_service(void) {
    pcb_sense_service();
#if CONTROL_DUAL_AXIS
    for(unsigned i=0;i<2;i++) { uint32_t m=board_lock();board_select_axis(i);board_unlock(m);board_service_axis(); }
#else
    board_service_axis();
#endif
}
void board_leds(bool armed,bool waiting,bool fault,uint32_t now) {
    bool blue=((now/500u)&1u)!=0;
    bool green=armed || (waiting && ((now/250u)&1u));
    bool red=fault;
    if (PCB_STAGE==PCB_IO_CHECK) {
        green=board_arm_pressed(); red=board_stop_pressed() || fault;
    }
    HAL_GPIO_WritePin(GPIOD,PCB_BLUE,blue?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD,PCB_GREEN,green?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD,PCB_RED,red?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_3,(GPIOG->ODR&GPIO_PIN_4)?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_10,(GPIOG->ODR&GPIO_PIN_7)?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_2,(PCB_TEST_AXIS==0 && fault)?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_11,(PCB_TEST_AXIS==1 && fault)?GPIO_PIN_SET:GPIO_PIN_RESET);
}

void EXTI9_5_IRQHandler(void) {
    if (__HAL_GPIO_EXTI_GET_IT(PCB_STOP)!=0u) {
        __HAL_GPIO_EXTI_CLEAR_IT(PCB_STOP); app_stop();
    }
#if CONTROL_DUAL_AXIS || PCB_TEST_AXIS == 1 || PCB_STAGE == PCB_SENSOR_CHECK
    if (__HAL_GPIO_EXTI_GET_IT(PCB_NFAULT)!=0u) {
        __HAL_GPIO_EXTI_CLEAR_IT(PCB_NFAULT);
        /* Wake has a normal fault pulse. Before PWM, SPI/live checks gate it.
         * During drive, latch even a brief pulse; no automatic restart. */
        if ((PCB_STAGE==PCB_MOTOR_RUN && (bridge_authorized
#if CONTROL_DUAL_AXIS
            || axes[0].authorized || axes[1].authorized
#endif
            )) ||
            (PCB_STAGE==PCB_SENSOR_CHECK && driver_ready)) driver_trip(DRIVER_TRIP_NFAULT_EDGE);
    }
#endif
}

#if PCB_TEST_AXIS == 0
void TIM1_BRK_IRQHandler(void) {
    if ((PCB_PWM_TIMER->SR&(TIM_SR_BIF|TIM_SR_B2IF))!=0) {
        driver_trip(DRIVER_TRIP_TIM1_BREAK); __HAL_TIM_CLEAR_FLAG(&htim_pwm,TIM_FLAG_BREAK|TIM_FLAG_BREAK2);
    }
}
#endif

void board_watchdog_start(void) {
    watchdog.Instance=IWDG1; watchdog.Init.Prescaler=IWDG_PRESCALER_32;
    watchdog.Init.Reload=999; watchdog.Init.Window=IWDG_WINDOW_DISABLE; check(HAL_IWDG_Init(&watchdog));
}

void board_watchdog_feed(void) { (void)HAL_IWDG_Refresh(&watchdog); }

void Error_Handler(void) { board_trip(FAULT_INTERNAL); __disable_irq(); for (;;) {} }
