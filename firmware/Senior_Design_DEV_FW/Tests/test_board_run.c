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
static uint32_t adc_delay;
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
    in->adc_ms=now-adc_delay; in->adc_valid=adc_good; *raw=adc;
    in->driver_good=driver_good;
    in->hard_fault=external_fault!=FAULT_NONE; in->external_fault=external_fault;
    /* No encoder. */
}
static app_status_t status(void) { app_status_t s; app_status(&s); return s; }
static void ticks(unsigned n) { while(n--) { ++now; app_foreground(); app_control_tick(); } }
static void pot(float v) { adc=(uint16_t)(v/DEMO_ADC_VREF*65535.0f); }
static void boot(bool high,float volts) {
    now=0; adc_delay=0; external_fault=FAULT_NONE; adc_good=driver_good=true;
    stop_latch=true; run_high=high; pot(volts); app_init();
}
static void arm(void) {
    run_high=false; pot(1.5f); ticks(25);
    run_high=true; ticks(DEMO_OPEN_NEUTRAL_DWELL_MS+25u);
}
static void run_request(float voltage) {
    run_high=false; pot(voltage); ticks(25); run_high=true; ticks(25);
}
int main(void) {
    /* Reset while RUN high never authorizes, even after neutral is reached. */
    boot(true,3.0f); ticks(200); pot(1.5f); ticks(300);
    assert(status().motor.state==MOTOR_DISABLED && !output.enable && !output.pwm);
    /* A fresh RUN outside neutral waits automatically; center must remain stable. */
    run_request(3.0f);
    assert(status().waiting_neutral && status().motor.state==MOTOR_DISABLED && !output.enable);
    pot(1.5f); ticks(100); pot(0.0f); ticks(30);
    assert(status().waiting_neutral && status().motor.state==MOTOR_DISABLED);
    pot(1.5f); ticks(DEMO_OPEN_NEUTRAL_DWELL_MS-1u);
    assert(status().motor.state==MOTOR_DISABLED && !output.enable);
    ticks(2); assert(status().motor.state==MOTOR_OPEN_IDLE && !status().waiting_neutral && !output.enable);
    assert(!app_request(ACTION_ARM) && !app_request(ACTION_CLEAR_FAULT));
    assert(!app_mode(MODE_CLOSED_LOOP) && !app_source(SOURCE_ETHERNET));
    pot(2.35f); ticks(2500);
    assert(status().motor.state==MOTOR_OPEN_RUNNING && output.pwm && status().motor.open_speed>0);
    pot(0.65f); bool coast_seen=false,reverse_seen=false;
    for (unsigned i=0;i<7000;++i) {
        ticks(1);
        if(status().motor.state==MOTOR_OPEN_COAST) { coast_seen=true; assert(!output.enable && !output.pwm); }
        if(status().motor.state==MOTOR_OPEN_RUNNING && status().motor.open_speed<0) { reverse_seen=true; break; }
    }
    assert(coast_seen && reverse_seen);
    run_high=false; ticks(1); assert(status().motor.state==MOTOR_DISABLED && !output.pwm);
    run_high=true; pot(1.5f); ticks(300); assert(status().motor.state==MOTOR_DISABLED); /* Short LOW cannot rearm. */
    arm(); pot(2.0f); ticks(40); assert(status().motor.state==MOTOR_OPEN_ALIGN);
    run_high=false; ticks(1); assert(!output.enable && status().motor.state==MOTOR_DISABLED);
    arm(); pot(2.0f); ticks(2500); driver_good=false; ticks(1);
    assert(status().motor.state==MOTOR_FAULT && !output.pwm);
    driver_good=true; arm(); assert(status().motor.state==MOTOR_FAULT && !output.enable);
    assert(!app_request(ACTION_CLEAR_FAULT));
    boot(true,0.04f); ticks(100); pot(1.5f); ticks(300); assert(status().motor.state==MOTOR_DISABLED);
    arm(); pot(2.0f); ticks(2500); pot(1.5f); ticks(4000);
    assert(status().motor.state==MOTOR_OPEN_IDLE && !output.enable);
    pot(0.65f); ticks(2500); assert(output.pwm && status().motor.open_speed<0);
    app_stop(); assert(!output.pwm); ticks(1); assert(status().motor.state==MOTOR_DISABLED);
    pot(1.5f); ticks(300); assert(status().motor.state==MOTOR_DISABLED);
    /* Low-side startup request also waits; invalid/stale samples reset dwell. */
    boot(false,0.04f); run_request(0.04f);
    assert(status().waiting_neutral && !output.enable);
    pot(1.5f); adc_good=false; ticks(300); assert(status().motor.state==MOTOR_DISABLED);
    adc_good=true; ticks(100); adc_delay=DEMO_ADC_MAX_AGE_MS+1u; ticks(100);
    adc_delay=0; ticks(DEMO_OPEN_NEUTRAL_DWELL_MS-1u); assert(status().motor.state==MOTOR_DISABLED);
    ticks(2); assert(status().motor.state==MOTOR_OPEN_IDLE);
    boot(false,3); run_request(3); pot(1.5f); ticks(100);
    app_stop(); ticks(1); ticks(300); assert(!status().waiting_neutral && status().motor.state==MOTOR_DISABLED);
    run_request(0.04f); run_high=false; ticks(1);
    assert(!status().waiting_neutral && !output.enable);
    /* Foreground expiry breaks the center dwell; recovery needs a new full dwell. */
    boot(false,3.0f); run_request(3.0f); pot(1.5f); ticks(60);
    for (unsigned i=0;i<150;++i) { ++now; app_control_tick(); }
    assert(status().waiting_neutral && status().motor.state==MOTOR_DISABLED && !output.enable);
    ticks(DEMO_OPEN_NEUTRAL_DWELL_MS-1u); assert(status().motor.state==MOTOR_DISABLED);
    ticks(2); assert(status().motor.state==MOTOR_OPEN_IDLE);
    /* A hard fault cancels a waiting request, and center cannot clear the latch. */
    boot(false,3.0f); run_request(3.0f); board_trip(FAULT_DRIVER); ticks(1);
    assert(status().motor.state==MOTOR_FAULT && !status().waiting_neutral && !output.enable);
    external_fault=FAULT_NONE; pot(1.5f); ticks(300);
    assert(status().motor.state==MOTOR_FAULT && !output.enable);
    /* Neutral dwell across HAL tick wrap is still a full interval. */
    boot(false,1.5f); now=UINT32_MAX-40u; app_init();
    ticks(25); run_high=true; ticks(DEMO_OPEN_NEUTRAL_DWELL_MS+25u);
    assert(status().motor.state==MOTOR_OPEN_IDLE);
    run_switch_t s={0}; uint32_t t=UINT32_MAX-10u;
    assert(run_switch_step(&s,false,t)==ACTION_DISABLE);
    assert(run_switch_step(&s,false,t+20u)==ACTION_DISABLE);
    assert(run_switch_step(&s,true,t+21u)==ACTION_NONE);
    assert(run_switch_step(&s,true,t+41u)==ACTION_ARM);
    puts("PASS: actual app RUN boot inhibit, automatic 200 ms neutral wait/dwell restart, +/- spin/reversal, low/stop cancellation, invalid/stale ADC, faults/reset and time wrap.");
    return 0;
}
