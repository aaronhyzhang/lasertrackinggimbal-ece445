/* Diagnostic acquisition only. No feedback-to-PWM path, DMA or OTP writes. */
#include "pcb_sense.h"
#include "board.h"
#include "pcb_config.h"
#include "control_config.h"
#include "stm32h7xx_ll_adc.h"
#include <math.h>
#include <string.h>
ADC_HandleTypeDef hadc1,hadc2,hadc3;
I2C_HandleTypeDef hi2c1,hi2c2;
volatile feedback_t g_feedback;
volatile telemetry_t g_telemetry;
typedef struct {
    ADC_HandleTypeDef *handle;
    uint8_t length,slot;
    uint16_t pending[9],raw[9];
    uint32_t sample_ms,started_us,count,errors,max_us;
    bool active,valid,failed,restart;
} adc_stream_t;
static volatile adc_stream_t streams[3];
typedef struct {
    I2C_HandleTypeDef *handle;
    uint8_t bytes[3],kind;
    bool busy,failed,reinitializing,raw_valid;
    uint32_t started_us,retry_ms,configuration_ms,magnitude_ms;
    uint32_t raw_sequence,processed_sequence,raw_us;
    uint16_t raw;
    uint8_t status;
    sense_encoder_t encoder;
} encoder_bus_t;
static volatile encoder_bus_t buses[2];
static sense_unwrap_t unwrap[2];
static sense_encoder_t encoder_state[2]; /* Foreground is sole derived-data writer. */
static sense_zero_t zero[2];
static volatile bool zero_ready[2];
static volatile uint32_t zero_ready_epoch[2];
static uint32_t zero_sequence[2],published_ms;
static bool cold_start;
static volatile uint16_t fast_raw[3];
static volatile uint32_t fast_us,fast_sequence;
static volatile bool fast_window;
static volatile uint32_t fast_timer_count,fast_timer_cr1;
static float control_reference=NAN,control_bus=NAN;
static uint32_t control_analog_ms;
static bool control_analog_valid,control_temp_valid;
static float control_temp;

static void check(HAL_StatusTypeDef s) { if (s!=HAL_OK) Error_Handler(); }
static telemetry_output_t telemetry_output(const sense_axis_t *axis) {
    return (telemetry_output_t){
        .encoder_angle_deg=axis->encoder.angle_deg,
        .encoder_position_deg=axis->encoder.position_deg,
        .encoder_velocity_rpm=axis->encoder.velocity_rpm,
        .phase_a={.current_a=axis->current_a[0],.voltage_v=axis->phase_v[0]},
        .phase_b={.current_a=axis->current_a[1],.voltage_v=axis->phase_v[1]},
        .phase_c={.current_a=axis->current_a[2],.voltage_v=axis->phase_v[2]},
        .temperature_c=axis->temperature_c,
        .status={.encoder_angle_valid=axis->encoder.valid,
                 .encoder_position_valid=axis->encoder.position_valid,
                 .encoder_velocity_valid=axis->encoder.velocity_valid,
                 .current_calibrated=axis->current_zero_valid,
                 .current_valid=axis->current_valid,
                 .current_window_valid=axis->current_window_valid,
                 .phase_voltage_valid=axis->phase_valid,
                 .temperature_valid=axis->temperature_valid,
                 .current_calibration_state=axis->current_zero_state,
                 .current_calibration_last_reject=axis->current_zero_last_reject,
                 .current_calibration_samples=axis->current_zero_samples}
    };
}
static telemetry_t telemetry_snapshot(const feedback_t *feedback) {
    return (telemetry_t){
        .output_j3=telemetry_output(&feedback->axis[0]),
        .output_j4=telemetry_output(&feedback->axis[1]),
        .bus_voltage_v=feedback->bus_v,
        .pot1_voltage_v=feedback->pot_v[0],
        .pot2_voltage_v=feedback->pot_v[1],
        .status={.bus_voltage_valid=feedback->bus_valid,.pot_voltage_valid=feedback->pot_valid}
    };
}
static void analog(GPIO_TypeDef *port,uint32_t pins) {
    GPIO_InitTypeDef g={0};g.Pin=pins;g.Mode=GPIO_MODE_ANALOG;g.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(port,&g);
}
static void adc_setup(unsigned index,ADC_HandleTypeDef *h,ADC_TypeDef *instance,
                      const uint32_t *channels,unsigned length) {
    static const uint32_t ranks[]={ADC_REGULAR_RANK_1,ADC_REGULAR_RANK_2,
        ADC_REGULAR_RANK_3,ADC_REGULAR_RANK_4,ADC_REGULAR_RANK_5,
        ADC_REGULAR_RANK_6,ADC_REGULAR_RANK_7,ADC_REGULAR_RANK_8,ADC_REGULAR_RANK_9};
    h->Instance=instance;
    /* PLL2P is 50 MHz. Revision V adds a fixed /2 after PRESC (RM0433,
     * ADC clocks); compensate so the analog ADC clock stays at 12.5 MHz.
     * Without this, a 387.5-cycle regular sample takes >62 us and is
     * repeatedly reset by the 50-us injected trigger, starving the scan.
     * The injected frame must also retain its intended 6-us duration. */
    h->Init.ClockPrescaler=HAL_GetREVID()==REV_ID_V?
        ADC_CLOCK_ASYNC_DIV2:ADC_CLOCK_ASYNC_DIV4;
    h->Init.Resolution=ADC_RESOLUTION_16B;h->Init.ScanConvMode=ADC_SCAN_ENABLE;
    h->Init.EOCSelection=ADC_EOC_SINGLE_CONV;h->Init.NbrOfConversion=length;
    /* One rank per software trigger. A fast injected-control ISR can delay
     * reading DR; do not let the next regular rank overwrite it meanwhile. */
    h->Init.DiscontinuousConvMode=ENABLE;h->Init.NbrOfDiscConversion=1;
    h->Init.ExternalTrigConv=ADC_SOFTWARE_START;
    h->Init.ExternalTrigConvEdge=ADC_EXTERNALTRIGCONVEDGE_NONE;
    h->Init.ConversionDataManagement=ADC_CONVERSIONDATA_DR;
    h->Init.Overrun=ADC_OVR_DATA_PRESERVED; /* Reject overrun; never relabel ranks. */
    check(HAL_ADC_Init(h));
    if (index==0) {
        ADC_MultiModeTypeDef multi={0};multi.Mode=ADC_MODE_INDEPENDENT;
        check(HAL_ADCEx_MultiModeConfigChannel(h,&multi));
    }
    ADC_ChannelConfTypeDef c={0};c.SamplingTime=ADC_SAMPLETIME_387CYCLES_5;
    c.SingleDiff=ADC_SINGLE_ENDED;c.OffsetNumber=ADC_OFFSET_NONE;
    for (unsigned i=0;i<length;++i) {
        c.Channel=channels[i];c.Rank=ranks[i];check(HAL_ADC_ConfigChannel(h,&c));
    }
    check(HAL_ADCEx_Calibration_Start(h,ADC_CALIB_OFFSET_LINEARITY,ADC_SINGLE_ENDED));
    streams[index].handle=h;streams[index].length=(uint8_t)length;
}
static void i2c_setup(I2C_HandleTypeDef *h,I2C_TypeDef *instance) {
    h->Instance=instance;
    /* Conservative ~100 kHz at the established 50 MHz D2PCLK1 kernel.
     * Measure rise times on the assembled harness before increasing speed. */
    h->Init.Timing=0x40932F35u;h->Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
    h->Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;
    h->Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;h->Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    check(HAL_I2C_Init(h));
    check(HAL_I2CEx_ConfigAnalogFilter(h,I2C_ANALOGFILTER_ENABLE));
    check(HAL_I2CEx_ConfigDigitalFilter(h,0));
}
void pcb_sense_init(void) {
    /* No converted value is available before the first acquisition. */
    feedback_t unavailable={0};unavailable.bus_v=NAN;
    unavailable.pot_v[0]=unavailable.pot_v[1]=NAN;
    for (unsigned i=0;i<2;++i) {
        sense_axis_t *axis=&unavailable.axis[i];
        axis->encoder.angle_deg=axis->encoder.position_deg=axis->encoder.velocity_rpm=NAN;
        axis->temperature_c=NAN;
        for (unsigned j=0;j<3;++j) axis->current_a[j]=axis->phase_v[j]=NAN;
    }
    g_telemetry=telemetry_snapshot(&unavailable);
    __HAL_RCC_GPIOF_CLK_ENABLE();
    analog(GPIOA,GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_6);
    analog(GPIOB,GPIO_PIN_1);analog(GPIOC,GPIO_PIN_0);
    analog(GPIOF,GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
        GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13);
    __HAL_RCC_ADC12_CLK_ENABLE();__HAL_RCC_ADC3_CLK_ENABLE();
    static const uint32_t a1[]={ADC_CHANNEL_18,ADC_CHANNEL_16,ADC_CHANNEL_5,
        ADC_CHANNEL_2,ADC_CHANNEL_6,ADC_CHANNEL_10,ADC_CHANNEL_15,ADC_CHANNEL_3};
    static const uint32_t a2[]={ADC_CHANNEL_2};
    static const uint32_t a3[]={ADC_CHANNEL_4,ADC_CHANNEL_8,ADC_CHANNEL_3,
        ADC_CHANNEL_7,ADC_CHANNEL_2,ADC_CHANNEL_6,ADC_CHANNEL_9,
        ADC_CHANNEL_5,ADC_CHANNEL_VREFINT};
    adc_setup(0,&hadc1,ADC1,a1,8);adc_setup(1,&hadc2,ADC2,a2,1);
    adc_setup(2,&hadc3,ADC3,a3,9);
    /* Selected currents only. Injected conversion preempts the slow regular
     * scan; ADC2 bus and ADC3 telemetry continue independently. Sampling time
     * is a per-channel setting and also applies to the regular current ranks. */
    ADC_InjectionConfTypeDef inj={0};
    inj.InjectedNbrOfConversion=3;inj.InjectedSamplingTime=ADC_SAMPLETIME_16CYCLES_5;
    inj.InjectedSingleDiff=ADC_SINGLE_ENDED;inj.InjectedOffsetNumber=ADC_OFFSET_NONE;
    inj.ExternalTrigInjecConv=PCB_TEST_AXIS?ADC_EXTERNALTRIGINJEC_T8_TRGO2:ADC_EXTERNALTRIGINJEC_T1_TRGO2;
    inj.ExternalTrigInjecConvEdge=ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;
    inj.QueueInjectedContext=DISABLE;inj.AutoInjectedConv=DISABLE;
    static const uint32_t channels[2][3]={{ADC_CHANNEL_5,ADC_CHANNEL_2,ADC_CHANNEL_6},
        {ADC_CHANNEL_10,ADC_CHANNEL_15,ADC_CHANNEL_3}};
    static const uint32_t ranks[3]={ADC_INJECTED_RANK_1,ADC_INJECTED_RANK_2,ADC_INJECTED_RANK_3};
    for (unsigned i=0;i<3;++i) {
        inj.InjectedChannel=channels[PCB_TEST_AXIS][i];inj.InjectedRank=ranks[i];
        check(HAL_ADCEx_InjectedConfigChannel(&hadc1,&inj));
    }
    HAL_NVIC_SetPriority(ADC_IRQn,1,0);HAL_NVIC_EnableIRQ(ADC_IRQn);
    HAL_NVIC_SetPriority(ADC3_IRQn,3,0);HAL_NVIC_EnableIRQ(ADC3_IRQn);
    __HAL_RCC_I2C1_CLK_ENABLE();__HAL_RCC_I2C2_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Mode=GPIO_MODE_AF_OD;g.Pull=GPIO_NOPULL;
    g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=GPIO_AF4_I2C1;
    g.Pin=GPIO_PIN_6|GPIO_PIN_7;HAL_GPIO_Init(GPIOB,&g);
    g.Alternate=GPIO_AF4_I2C2;g.Pin=GPIO_PIN_0|GPIO_PIN_1;HAL_GPIO_Init(GPIOF,&g);
    i2c_setup(&hi2c1,I2C1);i2c_setup(&hi2c2,I2C2);
    buses[0].handle=&hi2c1;buses[1].handle=&hi2c2;
    const IRQn_Type interrupts[]={I2C1_EV_IRQn,I2C1_ER_IRQn,I2C2_EV_IRQn,I2C2_ER_IRQn};
    for (unsigned i=0;i<4;++i) {
        HAL_NVIC_SetPriority(interrupts[i],3,0);HAL_NVIC_EnableIRQ(interrupts[i]);
    }
}
void pcb_sense_start(void) {
    /* HAL's first ADC enable polls ADRDY using SysTick. Do that in foreground,
     * where SysTick can preempt; never enter that wait from the control ISR. */
    cold_start=true;pcb_sense_tick();cold_start=false;
    check(HAL_ADCEx_InjectedStart_IT(&hadc1));
    /* Regular scan uses EOC per rank. Injected must use JEOS only, so all three
     * JDRs belong to one trigger. HAL_ADC_Start_IT preserves these injected bits. */
    __HAL_ADC_DISABLE_IT(&hadc1,ADC_IT_JEOC);__HAL_ADC_ENABLE_IT(&hadc1,ADC_IT_JEOS);
}
void pcb_sense_tick(void) {
    uint32_t now=HAL_GetTick(),us=board_us();
    for (unsigned i=0;i<3;++i) {
        volatile adc_stream_t *s=&streams[i];
        if (s->active && !s->failed && us-s->started_us>3000u) {
            s->valid=false;s->failed=true;++s->errors; /* Reset required, no rank guessing. */
        }
        if (!s->active && !s->failed) {
            if (!cold_start && !LL_ADC_IsEnabled(s->handle->Instance)) {
                s->valid=false;s->failed=true;++s->errors;continue;
            }
            s->slot=0;s->restart=false;s->active=true;s->started_us=us;
            if (HAL_ADC_Start_IT(s->handle)!=HAL_OK) {
                s->valid=false;s->active=false;s->failed=true;++s->errors;
            }
        }
    }
    for (unsigned i=0;i<2;++i) {
        volatile encoder_bus_t *b=&buses[i];
        if (b->busy && us-b->started_us>3000u && !b->failed) {
            b->failed=true;b->raw_valid=false;b->encoder.valid=false;++b->encoder.error_count;
            ++b->encoder.timeout_count;b->encoder.last_error=HAL_I2C_ERROR_TIMEOUT;
            b->retry_ms=now+20u;
        }
        if (b->busy || b->failed || b->reinitializing || (int32_t)(now-b->retry_ms)<0) continue;
        uint16_t address=0x0b;uint16_t length=3;b->kind=0;
        if (!b->encoder.configuration_valid || now-b->configuration_ms>=1000u) {
            b->kind=1;address=0x07;length=2;
        } else if (now-b->magnitude_ms>=100u) { b->kind=2;address=0x1a; }
        b->busy=true;b->started_us=us;
        HAL_StatusTypeDef status=HAL_I2C_Mem_Read_IT(b->handle,0x36u<<1,address,
            I2C_MEMADD_SIZE_8BIT,(uint8_t *)b->bytes,length);
        if (status!=HAL_OK) {
            b->busy=false;b->failed=true;b->raw_valid=false;b->retry_ms=now+20u;
            ++b->encoder.error_count;b->encoder.valid=false;
            b->encoder.last_error=status==HAL_BUSY?HAL_I2C_ERROR_TIMEOUT:HAL_I2C_GetError(b->handle);
        }
    }
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) {
    for (unsigned i=0;i<3;++i) {
        volatile adc_stream_t *s=&streams[i];
        if (s->handle!=h || !s->active || s->failed) continue;
        if (s->slot>=s->length) { s->failed=true;s->valid=false;++s->errors;return; }
        s->pending[s->slot++]=(uint16_t)HAL_ADC_GetValue(h);
        if (s->slot==s->length) {
            uint32_t mask=board_lock();
            for (unsigned j=0;j<s->length;++j) s->raw[j]=s->pending[j];
            s->sample_ms=HAL_GetTick();++s->count;s->valid=true;s->active=false;
            uint32_t used=board_us()-s->started_us;if (used>s->max_us) s->max_us=used;
            board_unlock(mask);
        } else s->restart=true; /* Resume after the vendor IRQ clears EOC/EOS. */
        return;
    }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h) {
    for (unsigned i=0;i<3;++i) if (streams[i].handle==h) {
        streams[i].valid=false;streams[i].failed=true;++streams[i].errors;
    }
}
void pcb_sense_adc_resume(ADC_HandleTypeDef *h) {
    for (unsigned i=0;i<3;++i) {
        volatile adc_stream_t *s=&streams[i];
        if (s->handle==h && s->restart && s->active && !s->failed) {
            s->restart=false;LL_ADC_REG_StartConversion(h->Instance);
        }
    }
}
void pcb_sense_pots(uint16_t raw[2],bool *valid,uint32_t *sample_ms) {
    uint32_t mask=board_lock();raw[0]=streams[0].raw[0];raw[1]=streams[0].raw[1];
    *valid=streams[0].valid;*sample_ms=streams[0].sample_ms;board_unlock(mask);
}
bool pcb_sense_current_zero_ready(unsigned axis,uint32_t epoch) {
    uint32_t mask=board_lock();
    bool ready=axis<2u && zero_ready[axis] && zero_ready_epoch[axis]==epoch;
    board_unlock(mask);return ready;
}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h) {
    for (unsigned i=0;i<2;++i) {
        volatile encoder_bus_t *b=&buses[i];
        if (b->handle!=h || !b->busy || b->failed) continue;
        uint32_t mask=board_lock(),us=board_us(),now=HAL_GetTick();
        if (us-b->started_us>3000u) {
            b->failed=true;b->raw_valid=false;b->retry_ms=now+20u;++b->encoder.error_count;
            ++b->encoder.timeout_count;b->encoder.last_error=HAL_I2C_ERROR_TIMEOUT;
        } else if (b->kind==0) {
            b->status=b->bytes[0];b->raw=((uint16_t)(b->bytes[1]&15u)<<8)|b->bytes[2];
            b->raw_us=us;b->raw_valid=true;++b->raw_sequence;
        } else if (b->kind==1) {
            b->encoder.configuration=((uint16_t)b->bytes[0]<<8)|b->bytes[1];
            b->encoder.configuration_valid=true;b->configuration_ms=now;
        } else {
            b->encoder.agc=b->bytes[0];
            b->encoder.magnitude=((uint16_t)(b->bytes[1]&15u)<<8)|b->bytes[2];
            b->magnitude_ms=now;
        }
        b->encoder.transaction_us=us-b->started_us;
        if (b->encoder.transaction_us>b->encoder.max_transaction_us)
            b->encoder.max_transaction_us=b->encoder.transaction_us;
        b->busy=false;board_unlock(mask);return;
    }
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h) {
    for (unsigned i=0;i<2;++i) if (buses[i].handle==h) {
        buses[i].failed=true;buses[i].raw_valid=false;buses[i].busy=false;buses[i].retry_ms=HAL_GetTick()+20u;
        ++buses[i].encoder.error_count;buses[i].encoder.valid=false;
        buses[i].encoder.last_error=HAL_I2C_GetError(h);
    }
}
static void encoder_recover(unsigned i) {
    volatile encoder_bus_t *b=&buses[i];
    if (!b->failed || (int32_t)(HAL_GetTick()-b->retry_ms)<0) return;
    uint32_t mask=board_lock();b->reinitializing=true;board_unlock(mask);
    IRQn_Type ev=i?I2C2_EV_IRQn:I2C1_EV_IRQn,er=i?I2C2_ER_IRQn:I2C1_ER_IRQn;
    HAL_NVIC_DisableIRQ(ev);HAL_NVIC_DisableIRQ(er);
    __HAL_I2C_DISABLE(b->handle);
    if (i==0) { __HAL_RCC_I2C1_FORCE_RESET();__HAL_RCC_I2C1_RELEASE_RESET(); }
    else { __HAL_RCC_I2C2_FORCE_RESET();__HAL_RCC_I2C2_RELEASE_RESET(); }
    b->handle->State=HAL_I2C_STATE_RESET;
    i2c_setup(b->handle,i?I2C2:I2C1);
    HAL_NVIC_ClearPendingIRQ(ev);HAL_NVIC_ClearPendingIRQ(er);
    mask=board_lock();
    b->busy=false;b->failed=false;b->reinitializing=false;
    b->encoder.configuration_valid=false;
    /* A transport failure may have hidden turns; never claim continuous position. */
    if (unwrap[i].initialized) unwrap[i].continuity_lost=true;
    HAL_NVIC_EnableIRQ(ev);HAL_NVIC_EnableIRQ(er);board_unlock(mask);
}
void pcb_sense_service(void) {
    uint32_t now=HAL_GetTick(),us=board_us();
    for (unsigned i=0;i<2;++i) {
        encoder_recover(i);
        uint32_t mask=board_lock();
        volatile encoder_bus_t *b=&buses[i];
        bool received=b->raw_sequence!=b->processed_sequence;
        bool failed=b->failed || !b->raw_valid;
        uint16_t raw=b->raw;uint8_t status=b->status;uint32_t stamp=b->raw_us;
        b->processed_sequence=b->raw_sequence;
        board_unlock(mask);
        if (failed && unwrap[i].initialized) unwrap[i].continuity_lost=true;
        if (received && !failed) sense_encoder_update(&unwrap[i],&encoder_state[i],raw,status,stamp);
    }
    adc_stream_t a[3];sense_driver_t drivers[2];
    uint32_t mask=board_lock();
    for (unsigned i=0;i<3;++i) a[i]=streams[i];
    board_unlock(mask);
    for (unsigned i=0;i<2;++i) board_feedback_driver(i,&drivers[i]);
    /* IRQs may publish newer samples while this foreground snapshot is copied.
     * Measure age afterwards, so a new timestamp cannot underflow unsigned age. */
    now=HAL_GetTick();us=board_us();
    bool fresh[3];
    for (unsigned i=0;i<3;++i) fresh[i]=a[i].valid && now-a[i].sample_ms<=SENSE_ADC_MAX_AGE_MS;
    float reference=fresh[2]?sense_reference_volts(a[2].raw[8],*VREFINT_CAL_ADDR):NAN;
    for (unsigned i=0;i<2;++i) {
        float v[3];for (unsigned j=0;j<3;++j) v[j]=sense_pin_volts(a[0].raw[2+i*3+j],reference);
        if (zero_sequence[i]!=a[0].count || zero[i].epoch!=drivers[i].epoch || !drivers[i].awake) {
            zero_sequence[i]=a[0].count;sense_zero_update(&zero[i],v,&drivers[i],fresh[0],reference);
            mask=board_lock();zero_ready_epoch[i]=zero[i].epoch;zero_ready[i]=zero[i].valid;
            board_unlock(mask);
        }
    }
    mask=board_lock();control_reference=reference;
    control_bus=11.0f*sense_pin_volts(a[1].raw[0],reference);
    control_analog_valid=fresh[0] && fresh[1] && fresh[2] && isfinite(reference) && a[1].raw[0]<65503u;
    control_analog_ms=now;
    float ohms;
    control_temp_valid=sense_temperature(a[2].raw[6+PCB_TEST_AXIS],&ohms,&control_temp) && fresh[2];
    board_unlock(mask);
    if (now-published_ms<50u) return;
    published_ms=now;feedback_t f={0};f.snapshot_ms=now;
    f.snapshot_count=g_feedback.snapshot_count+1;f.sensor_check=PCB_STAGE==PCB_SENSOR_CHECK;
    f.adc_reference_v=reference;f.reference_valid=isfinite(reference);
    for (unsigned i=0;i<3;++i) {
        f.adc_age_ms[i]=a[i].count?now-a[i].sample_ms:UINT32_MAX;
        f.adc_count[i]=a[i].count;f.adc_errors[i]=a[i].errors;
        if (a[i].max_us>f.max_acquisition_us) f.max_acquisition_us=a[i].max_us;
    }
    f.pot_valid=fresh[0] && f.reference_valid;
    for (unsigned j=0;j<2;++j) { f.pot_raw[j]=a[0].raw[j];f.pot_v[j]=sense_pin_volts(f.pot_raw[j],reference); }
    f.vrefint_raw=a[2].raw[8];f.bus_raw=a[1].raw[0];
    f.bus_pin_v=sense_pin_volts(f.bus_raw,reference);f.bus_v=11.0f*f.bus_pin_v;
    f.bus_valid=fresh[1] && f.reference_valid && f.bus_raw<65503u;
    for (unsigned i=0;i<2;++i) {
        sense_axis_t *o=&f.axis[i];o->driver=drivers[i];
        o->encoder=encoder_state[i];
        mask=board_lock();sense_encoder_t diagnostics=buses[i].encoder;
        bool transport_ok=buses[i].raw_valid && !buses[i].failed;
        board_unlock(mask);
        o->encoder.configuration=diagnostics.configuration;
        o->encoder.configuration_valid=diagnostics.configuration_valid;
        o->encoder.agc=diagnostics.agc;o->encoder.magnitude=diagnostics.magnitude;
        o->encoder.error_count=diagnostics.error_count;o->encoder.timeout_count=diagnostics.timeout_count;
        o->encoder.last_error=diagnostics.last_error;o->encoder.transaction_us=diagnostics.transaction_us;
        o->encoder.max_transaction_us=diagnostics.max_transaction_us;
        sense_encoder_age(&o->encoder,us);
        if (!transport_ok) o->encoder.valid=o->encoder.velocity_valid=false;
        if (!o->encoder.valid) o->encoder.position_valid=false;
        o->current_age_ms=f.adc_age_ms[0];o->phase_age_ms=f.adc_age_ms[2];
        o->current_zero_samples=zero[i].count;o->current_zero_valid=zero[i].valid;
        o->current_zero_state=zero[i].state;o->current_zero_last_reject=zero[i].last_reject;
        o->current_zero_reject_mask=zero[i].reject_mask;
        for (unsigned j=0;j<3;++j) o->current_zero_reject_v[j]=zero[i].reject_v[j];
        o->current_window_valid=!drivers[i].pwm_active;
        for (unsigned j=0;j<3;++j) {
            o->current_raw[j]=a[0].raw[2+i*3+j];
            o->current_v[j]=sense_pin_volts(o->current_raw[j],reference);
            o->current_zero_v[j]=zero[i].valid?zero[i].zero[j]:NAN;
            o->current_a[j]=zero[i].valid && drivers[i].awake && drivers[i].configured && drivers[i].gain_v_per_v==20.0f?
                -sense_current_amps(o->current_v[j],zero[i].zero[j]):NAN;
            if (!isfinite(o->current_v[j]) || o->current_v[j]<0.25f ||
                o->current_v[j]>reference-0.25f) o->current_saturated_mask|=(uint8_t)(1u<<j);
            o->phase_raw[j]=a[2].raw[i*3+j];
            o->phase_v[j]=11.0f*sense_pin_volts(o->phase_raw[j],reference);
            if (o->phase_raw[j]>=65503u) o->phase_saturated_mask|=(uint8_t)(1u<<j);
        }
        o->current_valid=fresh[0] && f.reference_valid && zero[i].valid &&
            drivers[i].awake && drivers[i].configured && drivers[i].gain_v_per_v==20.0f &&
            !o->current_saturated_mask && o->current_window_valid;
        o->phase_valid=fresh[2] && f.reference_valid && !o->phase_saturated_mask;
        o->temperature_raw=a[2].raw[6+i];o->temperature_v=sense_pin_volts(o->temperature_raw,reference);
        o->temperature_valid=sense_temperature(o->temperature_raw,&o->thermistor_ohm,&o->temperature_c)
            && fresh[2] && f.reference_valid;
    }
    /* The selected axis uses PWM-synchronous winding currents in the debugger
     * too. Unselected-axis regular scans remain diagnostic while inhibited. */
    control_input_t current={0};pcb_control_read(&current);
    sense_axis_t *selected=&f.axis[PCB_TEST_AXIS];
    selected->current_window_valid=current.current_window_valid;
    selected->current_age_ms=(current.now_us-current.current_us)/1000u;
    selected->current_valid=current.current_valid && current.now_us-current.current_us<=150u &&
        selected->driver.awake && selected->driver.configured && selected->driver.gain_v_per_v==20 &&
        zero_ready_epoch[PCB_TEST_AXIS]==selected->driver.epoch;
    for (unsigned j=0;j<3;++j) {
        selected->current_raw[j]=current.current_raw[j];
        selected->current_v[j]=sense_pin_volts(current.current_raw[j],reference);
        selected->current_a[j]=selected->current_zero_valid?current.current_a[j]:NAN;
    }
    telemetry_t telemetry=telemetry_snapshot(&f);
    mask=board_lock();g_feedback=f;g_telemetry=telemetry;board_unlock(mask);
}
void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }
void I2C2_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c2); }
void I2C2_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c2); }
void ADC3_IRQHandler(void) { HAL_ADC_IRQHandler(&hadc3);pcb_sense_adc_resume(&hadc3); }

/* Called by the vendor HAL before it clears JEOS. No waits or SPI in this path. */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *h) {
    if (h!=&hadc1 || !__HAL_ADC_GET_FLAG(h,ADC_FLAG_JEOS)) return;
    fast_raw[0]=(uint16_t)h->Instance->JDR1;fast_raw[1]=(uint16_t)h->Instance->JDR2;
    fast_raw[2]=(uint16_t)h->Instance->JDR3;
    fast_us=board_us();++fast_sequence;
    /* Sequence must finish on the descending half while every low side is on.
     * Cap duties at .65: rising trigger CNT2300, rank skew ~2 us, total6us.
     * Gate/CSA settling and actual trigger latency still require scope evidence. */
    TIM_TypeDef *timer=PCB_TEST_AXIS?TIM8:TIM1;
    fast_timer_cr1=timer->CR1;fast_timer_count=timer->CNT;
    fast_window=(fast_timer_cr1&TIM_CR1_DIR)!=0 && fast_timer_count>1650u && fast_timer_count<2450u;
    app_current_sample();
}
void pcb_control_read(control_input_t *in) {
    uint32_t mask=board_lock();
    volatile encoder_bus_t *e=&buses[PCB_TEST_AXIS];
    in->now_us=board_us();in->now_ms=HAL_GetTick();
    in->encoder_us=e->raw_us;in->encoder_raw=e->raw;in->encoder_status=e->status;
    in->encoder_transport=e->raw_valid && !e->failed && !e->reinitializing;
    in->analog_valid=control_analog_valid && in->now_ms-control_analog_ms<=20u;
    in->analog_age_ms=in->now_ms-control_analog_ms;
    in->adc_fresh_mask=in->adc_failed_mask=0;
    for (unsigned i=0;i<3;++i) {
        in->adc_errors[i]=streams[i].errors;
        if (streams[i].valid && in->now_ms-streams[i].sample_ms<=20u) in->adc_fresh_mask|=(uint8_t)(1u<<i);
        if (streams[i].failed) in->adc_failed_mask|=(uint8_t)(1u<<i);
    }
    in->bus_v=control_bus;in->temperature_valid=control_temp_valid;in->temperature_c=control_temp;
    in->current_us=fast_us;in->current_sequence=fast_sequence;
    in->current_window_valid=fast_window;
    in->current_timer_count=fast_timer_count;in->current_timer_cr1=fast_timer_cr1;
    in->current_adc_clock_prescaler=hadc1.Init.ClockPrescaler;
    in->zero_ready=zero_ready[PCB_TEST_AXIS];
    /* Timer-triggered acquisition is independent of callback latency. In
     * sinusoidal voltage mode retain the callback-window diagnostic without
     * rejecting the frame solely because software observed CNT late. Current
     * regulation retains the strict window requirement. Freshness, zero,
     * clipping, ADC failure and current trips remain mandatory in both modes. */
    in->current_valid=in->analog_valid && in->zero_ready && fast_sequence &&
        (!CONTROL_CURRENT_PI_ENABLED || fast_window) && !streams[0].failed;
    for (unsigned j=0;j<3;++j) {
        /* Low-side SP->SN shunt current is opposite the winding current used
         * by commutation math. This project's telemetry uses winding polarity too. */
        in->current_raw[j]=fast_raw[j];
        float voltage=sense_pin_volts(in->current_raw[j],control_reference);
        in->current_a[j]=(voltage-zero[PCB_TEST_AXIS].zero[j])/0.28f;
        if (!isfinite(voltage) || voltage<0.25f || voltage>control_reference-0.25f) in->current_valid=false;
    }
    board_unlock(mask);
}
