/* Execute actual acquisition code with scripted HAL completions and private MMIO.
 * Tests software routing/failures, not electrical ADC accuracy or I2C timing. */
#include "board.h"
#include "demo_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pcb_sense.c"
static unsigned fast_callbacks;
#ifndef TEST_ADC_REVISION
#define TEST_ADC_REVISION REV_ID_V
#endif
uint32_t HAL_GetREVID(void) { return TEST_ADC_REVISION; }
void app_current_sample(void) { ++fast_callbacks; }
__declspec(dllimport) void *VirtualAlloc(void *,size_t,unsigned long,unsigned long);
static uint32_t now_ms,now_us,irq_mask;
static uint16_t conversion[3];
static uint32_t channel_map[3][9];
static unsigned injected_ranks;
static unsigned ranks[3],starts[3],reads[2],recoveries;
static sense_driver_t driver[2];
static bool reject_zero,advance_during_snapshot;
static unsigned adc_index(ADC_HandleTypeDef *h) { return h==&hadc1?0:h==&hadc2?1:2; }
uint32_t board_lock(void) {
    uint32_t old=irq_mask;irq_mask=1;
    if (advance_during_snapshot) {
        advance_during_snapshot=false;++now_ms;now_us=now_ms*1000u;
        for (unsigned i=0;i<3;++i) { streams[i].sample_ms=now_ms;++streams[i].count; }
    }
    return old;
}
void board_unlock(uint32_t old) { irq_mask=old; }
uint32_t board_us(void) { return now_us; }
uint32_t HAL_GetTick(void) { return now_ms; }
void board_feedback_driver(unsigned axis,sense_driver_t *out) { *out=driver[axis]; }
void Error_Handler(void) { abort(); }
void HAL_NVIC_SetPriority(IRQn_Type i,uint32_t p,uint32_t s) { (void)i;(void)p;(void)s; }
void HAL_NVIC_EnableIRQ(IRQn_Type i) { (void)i; }
void HAL_NVIC_DisableIRQ(IRQn_Type i) { (void)i; }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type i) { (void)i; }
void HAL_GPIO_Init(GPIO_TypeDef *p,const GPIO_InitTypeDef *g) {
    if (g->Mode==GPIO_MODE_AF_OD) {
        assert(g->Pull==GPIO_NOPULL);
        assert((p==GPIOB && g->Pin==(GPIO_PIN_6|GPIO_PIN_7) && g->Alternate==GPIO_AF4_I2C1) ||
               (p==GPIOF && g->Pin==(GPIO_PIN_0|GPIO_PIN_1) && g->Alternate==GPIO_AF4_I2C2));
    } else assert(g->Mode==GPIO_MODE_ANALOG);
}
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h) {
    unsigned i=adc_index(h);assert(h->Init.Resolution==ADC_RESOLUTION_16B);
    unsigned divider=h->Init.ClockPrescaler==ADC_CLOCK_ASYNC_DIV2?2u:
        h->Init.ClockPrescaler==ADC_CLOCK_ASYNC_DIV4?4u:0u;
    assert(divider);
    if (HAL_GetREVID()==REV_ID_V) divider*=2u;
    unsigned analog_hz=50000000u/divider;
    assert(analog_hz==12500000u);
    /* A regular rank must fit between injected triggers. A complete current
     * frame must finish 6 us after the trigger to match the PWM window. */
    double regular_us=(387.5+8.5)*1000000.0/analog_hz;
    double injected_us=3.0*(16.5+8.5)*1000000.0/analog_hz;
    assert(regular_us+injected_us<50.0);
    assert(injected_us>=5.9 && injected_us<=6.1);
    assert(h->Init.DiscontinuousConvMode==ENABLE && h->Init.NbrOfDiscConversion==1);
    assert(h->Init.Overrun==ADC_OVR_DATA_PRESERVED);ranks[i]=0;return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_MultiModeConfigChannel(ADC_HandleTypeDef *h,ADC_MultiModeTypeDef *m) {
    assert(h==&hadc1 && m->Mode==ADC_MODE_INDEPENDENT);return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h,uint32_t m,uint32_t d) {
    (void)h;assert(m==ADC_CALIB_OFFSET_LINEARITY && d==ADC_SINGLE_ENDED);return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h,ADC_ChannelConfTypeDef *c) {
    unsigned i=adc_index(h);assert(c->SamplingTime==ADC_SAMPLETIME_387CYCLES_5);
    channel_map[i][ranks[i]++]=c->Channel;return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_InjectedConfigChannel(ADC_HandleTypeDef *h,ADC_InjectionConfTypeDef *c) {
    static const uint32_t expected[2][3]={{ADC_CHANNEL_5,ADC_CHANNEL_2,ADC_CHANNEL_6},
        {ADC_CHANNEL_10,ADC_CHANNEL_15,ADC_CHANNEL_3}};
    assert(h==&hadc1 && c->InjectedNbrOfConversion==3);
    assert(c->InjectedSamplingTime==ADC_SAMPLETIME_16CYCLES_5);
    assert(c->ExternalTrigInjecConv==((!CONTROL_DUAL_AXIS && PCB_TEST_AXIS)?ADC_EXTERNALTRIGINJEC_T8_TRGO2:ADC_EXTERNALTRIGINJEC_T1_TRGO2));
    assert(c->ExternalTrigInjecConvEdge==ADC_EXTERNALTRIGINJECCONV_EDGE_RISING);
    unsigned axis=CONTROL_DUAL_AXIS?injected_ranks/3:PCB_TEST_AXIS;
    assert(c->InjectedChannel==expected[axis][injected_ranks%3]);++injected_ranks;
    /* Scripted register encoding sufficient to check distinct context routing. */
    if(c->InjectedRank==ADC_INJECTED_RANK_1)h->Instance->JSQR=0;
    h->Instance->JSQR=(h->Instance->JSQR<<5)|(c->InjectedChannel&31u);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_InjectedStart_IT(ADC_HandleTypeDef *h) { assert(h==&hadc1);return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start_IT(ADC_HandleTypeDef *h) { h->Instance->CR|=ADC_CR_ADEN;++starts[adc_index(h)];return HAL_OK; }
uint32_t HAL_ADC_GetValue(const ADC_HandleTypeDef *h) { return conversion[h==&hadc1?0:h==&hadc2?1:2]; }
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) {
    assert(h->Init.Timing==0x40932F35u);h->State=HAL_I2C_STATE_READY;++recoveries;return HAL_OK;
}
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h,uint32_t f) {
    (void)h;assert(f==I2C_ANALOGFILTER_ENABLE);return HAL_OK;
}
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h,uint32_t f) { (void)h;assert(!f);return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *h,uint16_t address,uint16_t reg,
                                    uint16_t size,uint8_t *bytes,uint16_t length) {
    unsigned i=h==&hi2c1?0:1;assert(address==0x6c && size==I2C_MEMADD_SIZE_8BIT);
    assert((reg==0x0b && length==3) || (reg==7 && length==2) || (reg==0x1a && length==3));
    assert(bytes==(uint8_t *)buses[i].bytes);++reads[i];return HAL_OK;
}
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *h) { (void)h;return HAL_I2C_ERROR_AF; }
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h) { (void)h; }
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h) { (void)h; }
static void complete_adc(void) {
    uint16_t data[3][9]={{32768,12345,32768,32768,32768,32768,32768,32768},
        {43331},{0,20000,40000,10000,30000,50000,59577,59577,24000}};
    if (reject_zero) data[0][2+PCB_TEST_AXIS*3]=38000; /* About 1.91 V on phase A. */
    for (unsigned i=0;i<3;++i) if (streams[i].active && !streams[i].failed)
        for (unsigned j=0;j<streams[i].length;++j) {
            conversion[i]=data[i][j];now_us+=10;HAL_ADC_ConvCpltCallback(streams[i].handle);
            streams[i].handle->Instance->CR&=~ADC_CR_ADSTART;
            pcb_sense_adc_resume(streams[i].handle);
            /* LL intentionally writes zero to CR's read/set bits. On silicon
             * that preserves ADEN/JADSTART; plain host RAM needs that emulation. */
            streams[i].handle->Instance->CR|=ADC_CR_ADEN;
            assert(((streams[i].handle->Instance->CR&ADC_CR_ADSTART)!=0)==(j+1<streams[i].length));
        }
}
static void complete_encoders(unsigned skip_mask) {
    for (unsigned i=0;i<2;++i) if (buses[i].busy && !buses[i].failed && !(skip_mask&(1u<<i))) {
        if (buses[i].kind==0) {
            uint16_t raw=(uint16_t)((i?3000:100)+now_ms)&4095u;
            buses[i].bytes[0]=0x20;buses[i].bytes[1]=(uint8_t)(raw>>8);buses[i].bytes[2]=(uint8_t)raw;
        } else if (buses[i].kind==1) { buses[i].bytes[0]=0;buses[i].bytes[1]=0; }
        else { buses[i].bytes[0]=100;buses[i].bytes[1]=8;buses[i].bytes[2]=0; }
        now_us+=100;HAL_I2C_MemRxCpltCallback(buses[i].handle);
    }
}
static void cycle(unsigned skip_mask) {
    ++now_ms;now_us=now_ms*1000u;pcb_sense_tick();complete_adc();complete_encoders(skip_mask);
    TIM_TypeDef *timer=PCB_TEST_AXIS?TIM8:TIM1;timer->CR1=TIM_CR1_DIR;timer->CNT=2100;
    hadc1.Instance->JDR1=hadc1.Instance->JDR2=hadc1.Instance->JDR3=32768;
    hadc1.Instance->ISR=ADC_FLAG_JEOS;HAL_ADCEx_InjectedConvCpltCallback(&hadc1);
    pcb_sense_service();
}
int main(void) {
    assert(VirtualAlloc((void *)0x58000000u,0x30000,0x3000,4)==(void *)0x58000000u);
    assert(VirtualAlloc((void *)0x40000000u,0x30000,0x3000,4)==(void *)0x40000000u);
    assert(VirtualAlloc((void *)0x1ff10000u,0x10000,0x3000,4)==(void *)0x1ff10000u);
    *VREFINT_CAL_ADDR=24000;
    pcb_sense_init();
#if CONTROL_DUAL_AXIS
    assert(injected_ranks==6 && injected_contexts[0]!=injected_contexts[1]);
    hadc1.Instance->ISR=ADC_FLAG_JEOS;
    hadc1.Instance->JDR1=111;hadc1.Instance->JDR2=112;hadc1.Instance->JDR3=113;
    now_us=100;HAL_ADCEx_InjectedConvCpltCallback(&hadc1);
    assert(pcb_current_axis==0 && frames[0].raw[0]==111 && frames[1].sequence==0);
    assert(hadc1.Instance->JSQR==injected_contexts[1]);
    hadc1.Instance->JDR1=221;now_us=150;HAL_ADCEx_InjectedConvCpltCallback(&hadc1);
    assert(pcb_current_axis==1 && frames[1].raw[0]==221 && frames[0].raw[0]==111);
    assert(frames[0].us==100 && frames[1].us==150 && hadc1.Instance->JSQR==injected_contexts[0]);
    control_input_t in={0};pcb_control_read_axis(0,&in);assert(in.current_us==100 && in.current_raw[0]==111);
    pcb_control_read_axis(1,&in);assert(in.current_us==150 && in.current_raw[0]==221);
    puts("PASS alternating ADC contexts, independent frames/timestamps and axis read routing");return 0;
#endif
    assert(isnan(g_telemetry.output_j3.encoder_angle_deg));
    assert(isnan(g_telemetry.output_j4.phase_a.current_a));
    assert(!g_telemetry.output_j3.status.current_calibrated);
    assert(ranks[0]==8 && ranks[1]==1 && ranks[2]==9);
    const uint32_t a1[]={ADC_CHANNEL_18,ADC_CHANNEL_16,ADC_CHANNEL_5,ADC_CHANNEL_2,
        ADC_CHANNEL_6,ADC_CHANNEL_10,ADC_CHANNEL_15,ADC_CHANNEL_3};
    const uint32_t a3[]={ADC_CHANNEL_4,ADC_CHANNEL_8,ADC_CHANNEL_3,ADC_CHANNEL_7,
        ADC_CHANNEL_2,ADC_CHANNEL_6,ADC_CHANNEL_9,ADC_CHANNEL_5,ADC_CHANNEL_VREFINT};
    assert(!memcmp(channel_map[0],a1,sizeof(a1)) && !memcmp(channel_map[2],a3,sizeof(a3)));
    assert(channel_map[1][0]==ADC_CHANNEL_2);
    for (unsigned i=0;i<2;++i) driver[i]=(sense_driver_t){.awake=true,.configured=true,.epoch=1,.gain_v_per_v=20,.csa_register=640};
    pcb_sense_start();complete_adc();complete_encoders(0);pcb_sense_service();
    for (unsigned i=0;i<350;++i) cycle(0);

    assert(g_feedback.reference_valid && fabsf(g_feedback.bus_v-24)<0.01f);
    assert(g_feedback.pot_raw[0]==32768 && g_feedback.pot_raw[1]==12345);
    for (unsigned i=0;i<2;++i) {
        assert(g_feedback.axis[i].encoder.valid && g_feedback.axis[i].encoder.sample_count>300);
        assert(g_feedback.axis[i].current_zero_valid && g_feedback.axis[i].current_valid);
        assert(g_feedback.axis[i].temperature_valid && fabsf(g_feedback.axis[i].temperature_c-25)<0.02f);
        assert(g_feedback.axis[i].phase_valid && fabsf(g_feedback.axis[i].current_a[0])<0.001f);
    }
    /* ADC completion can be newer than foreground entry time. Fresh samples
     * must not appear stale because unsigned age subtraction underflows. */
    advance_during_snapshot=true;published_ms=now_ms-50;pcb_sense_service();
    assert(g_feedback.reference_valid && g_feedback.axis[1-PCB_TEST_AXIS].current_valid);
    assert(pcb_sense_current_zero_ready(0,driver[0].epoch));
    assert(!pcb_sense_current_zero_ready(0,driver[0].epoch+1u));
    assert(injected_ranks==3);
    assert((hadc1.Instance->IER&ADC_IT_JEOS)!=0 && !(hadc1.Instance->IER&ADC_IT_JEOC));
    TIM_TypeDef *timer=PCB_TEST_AXIS?TIM8:TIM1;
    timer->CR1=TIM_CR1_DIR;timer->CNT=2100;
    hadc1.Instance->JDR1=38328;hadc1.Instance->JDR2=32768;hadc1.Instance->JDR3=27208;
    hadc1.Instance->ISR=ADC_FLAG_JEOS;
    unsigned callbacks_before=fast_callbacks;HAL_ADCEx_InjectedConvCpltCallback(&hadc1);
    assert(fast_callbacks==callbacks_before+1);
    control_input_t control={0};pcb_control_read(&control);
    assert(control.current_valid && control.current_sequence==frames[PCB_TEST_AXIS].sequence);
    assert(control.current_timer_count==2100 && (control.current_timer_cr1&TIM_CR1_DIR));
    assert(fabsf(control.current_a[0]-1.0f)<0.01f && fabsf(control.current_a[2]+1.0f)<0.01f);
    assert(control.encoder_raw==buses[PCB_TEST_AXIS].raw); /* No 20Hz snapshot latency. */
    timer->CR1=0;HAL_ADCEx_InjectedConvCpltCallback(&hadc1);pcb_control_read(&control);
    assert(control.current_valid==!CONTROL_CURRENT_PI_ENABLED);
    assert(!control.current_window_valid); /* Always retain the diagnostic. */
    timer->CR1=TIM_CR1_DIR;timer->CNT=1400;
    HAL_ADCEx_InjectedConvCpltCallback(&hadc1);pcb_control_read(&control);
    assert(control.current_valid==!CONTROL_CURRENT_PI_ENABLED);
    /* Snapshot must retain the sample's timer position, not the live timer. */
    timer->CNT=2100;pcb_control_read(&control);
    assert(control.current_timer_count==1400 && !control.current_window_valid);
    uint32_t other_count=g_feedback.axis[0].encoder.sample_count;
    HAL_I2C_ErrorCallback(&hi2c2);published_ms=now_ms-50;pcb_sense_service();
    assert(!g_feedback.axis[1].encoder.valid);
    /* The compact view must retain the same converted diagnostic angle. */
    assert(g_telemetry.output_j4.encoder_angle_deg==g_feedback.axis[1].encoder.angle_deg);
    assert(!g_telemetry.output_j4.status.encoder_angle_valid);
    for (unsigned i=0;i<75;++i) cycle(0);
    assert(g_feedback.axis[0].encoder.sample_count>other_count && recoveries>=3);
    assert(g_feedback.axis[1].encoder.valid && !g_feedback.axis[1].encoder.position_valid);
    for (unsigned i=0;i<8;++i) cycle(2);
    assert(buses[1].encoder.timeout_count>0);
    driver[0].pwm_active=true;published_ms=now_ms-50;pcb_sense_service();
    assert(g_feedback.axis[0].current_valid==(PCB_TEST_AXIS==0));
    assert(g_feedback.axis[0].current_window_valid==(PCB_TEST_AXIS==0));
    assert(isfinite(g_feedback.axis[0].current_a[0]));
    assert(g_telemetry.output_j3.phase_a.current_a==g_feedback.axis[0].current_a[0]);
    assert(g_telemetry.output_j3.status.current_calibrated);
    assert(g_telemetry.output_j3.status.current_valid==(PCB_TEST_AXIS==0));
    assert(g_telemetry.output_j3.status.current_window_valid==(PCB_TEST_AXIS==0));
    HAL_ADC_ErrorCallback(&hadc1);unsigned old_starts=starts[0];
    for (unsigned i=0;i<60;++i) cycle(0);
    assert(!g_feedback.pot_valid && !g_feedback.axis[0].current_valid && g_feedback.bus_valid);
    assert(starts[0]==old_starts && g_feedback.adc_errors[0]==1);
    hadc2.Instance->CR&=~ADC_CR_ADEN;old_starts=starts[1];cycle(0);
    assert(streams[1].failed && starts[1]==old_starts); /* No HAL enable wait inside ISR. */
    now_ms+=100;now_us=now_ms*1000;pcb_sense_service();
    assert(!g_feedback.bus_valid && !g_feedback.reference_valid && !g_feedback.axis[0].encoder.valid);
    puts("PASS: actual ADC rank maps/coherent frames, both I2C routes, conversions/calibration, independent recovery, timeout, stale/overrun and PWM validity.");
}
