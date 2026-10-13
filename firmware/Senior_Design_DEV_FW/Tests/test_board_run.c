#include "demo_app.h"
#include "demo_config.h"
#include "run_switch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static bool run_high,adc_good=true,driver_good=true,stop_latch=true;
static motor_fault_t external_fault;
static uint16_t adc;
static motor_output_t output;
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t mask) { (void)mask; }
uint32_t HAL_GetTick(void) { return now; }
uint32_t board_us(void) { return now*1000u; }
bool board_run_high(void) { return run_high; }
void board_trip(motor_fault_t f) { external_fault=f; output.enable=output.pwm=false; }
void board_clear_trip(void) { external_fault=FAULT_NONE; }
void board_kill(void) { stop_latch=true; output.enable=output.pwm=false; }
void board_release_stop(void) { stop_latch=false; }
void board_apply(const motor_output_t *out) {
    output=*out;
    if (!run_high || stop_latch || external_fault!=FAULT_NONE) output.enable=output.pwm=false;
}
void board_sensor_tick(void) {}
void board_watchdog_feed(void) {}
void HAL_GPIO_WritePin(void *p,uint32_t pin,int val) { (void)p; (void)pin; (void)val; }
const char *network_status(void) { return "NOT_STARTED"; }
void board_inputs(motor_input_t *in,uint16_t *raw) {
    in->adc_ms=now; in->adc_valid=adc_good; *raw=adc;
    in->driver_good=driver_good;
    in->hard_fault=external_fault!=FAULT_NONE; in->external_fault=external_fault;
    /* No encoder. */
}
static app_status_t status(void) { app_status_t s; app_status(&s); return s; }
static void ticks(unsigned n) { while(n--) { ++now; app_foreground(); app_control_tick(); } }
static void pot(float v) { adc=(uint16_t)(v/DEMO_ADC_VREF*65535.0f); }
static void boot(bool high,float volts) {
    now=0; external_fault=FAULT_NONE; adc_good=driver_good=true;
    stop_latch=true; run_high=high; pot(volts); app_init();
}
static void arm(void) { run_high=false; pot(0.04f); ticks(25); run_high=true; ticks(25); }
int main(void) {
    boot(true,1.5f); ticks(200);
    assert(status().motor.state==MOTOR_DISABLED && !output.enable && !output.pwm);
    run_high=false; ticks(25); run_high=true; ticks(25); /* Bad high-pot edge consumed. */
    assert(status().motor.state==MOTOR_DISABLED);
    pot(0.04f); ticks(100); assert(status().motor.state==MOTOR_DISABLED);
    arm(); assert(status().motor.state==MOTOR_OPEN_IDLE && !output.enable);
    assert(!app_request(ACTION_ARM) && !app_request(ACTION_CLEAR_FAULT));
    assert(!app_mode(MODE_CLOSED_LOOP) && !app_source(SOURCE_ETHERNET));
    pot(0.75f); ticks(2500);
    assert(status().motor.state==MOTOR_OPEN_RUNNING && output.pwm && output.enable);
    assert(status().motor.open_speed>0 && !status().motor.calibrated);
    run_high=false; ticks(1); assert(status().motor.state==MOTOR_DISABLED && !output.pwm);
    run_high=true; pot(0.04f); ticks(100); assert(status().motor.state==MOTOR_DISABLED); /* Short low cannot rearm. */
    arm(); pot(0.75f); ticks(40); assert(status().motor.state==MOTOR_OPEN_ALIGN);
    run_high=false; ticks(1); assert(!output.enable && status().motor.state==MOTOR_DISABLED);
    arm(); pot(0.75f); ticks(2500); driver_good=false; ticks(1);
    assert(status().motor.state==MOTOR_FAULT && !output.pwm);
    driver_good=true; arm(); assert(status().motor.state==MOTOR_FAULT && !output.enable);
    assert(!app_request(ACTION_CLEAR_FAULT)); /* Fault remains latched; use reset. */
    boot(true,0.04f); ticks(100); assert(status().motor.state==MOTOR_DISABLED); arm();
    assert(status().motor.state==MOTOR_OPEN_IDLE);
    pot(0.75f); ticks(2500); pot(0.04f); ticks(4000);
    assert(status().motor.state==MOTOR_OPEN_IDLE && !output.enable); /* Pot returns to coast. */
    pot(0.75f); ticks(2500); assert(output.pwm); /* Authorized RUN-high pot restart realigns. */
    app_stop(); assert(!output.pwm); ticks(1); assert(status().motor.state==MOTOR_DISABLED);
    pot(0.04f); ticks(100); assert(status().motor.state==MOTOR_DISABLED);
    boot(false,0.04f); adc_good=false; ticks(25); run_high=true; ticks(25);
    adc_good=true; ticks(100); assert(status().motor.state==MOTOR_DISABLED); arm();
    assert(status().motor.state==MOTOR_OPEN_IDLE);
    run_switch_t s={0}; uint32_t t=UINT32_MAX-10u;
    assert(run_switch_step(&s,false,t)==ACTION_DISABLE);
    assert(run_switch_step(&s,false,t+20u)==ACTION_DISABLE);
    assert(run_switch_step(&s,true,t+21u)==ACTION_NONE);
    assert(run_switch_step(&s,true,t+41u)==ACTION_ARM);
    puts("PASS: actual app physical RUN boot/edge/debounce/40 mV ARM, GPIO stop, rejected high-pot/ADC edges, encoder-free spin, pot coast/restart and latched fault/reset.");
    return 0;
}
