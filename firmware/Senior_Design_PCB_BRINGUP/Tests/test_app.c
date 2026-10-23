#include "demo_app.h"
#include "demo_config.h"
#include "pcb_config.h"
#include <assert.h>
#include <stdio.h>
static uint32_t now,adc_delay;
static bool arm_pressed,stop_pressed,adc_good,driver_good,stop_latch;
static motor_fault_t external_fault;
static uint16_t adc[2];
static motor_output_t output;
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t mask) { (void)mask; }
uint32_t HAL_GetTick(void) { return now; }
uint32_t board_us(void) { return now*1000u; }
bool board_arm_pressed(void) { return arm_pressed; }
bool board_stop_pressed(void) { return stop_pressed; }
void board_trip(motor_fault_t f) { external_fault=f; output.enable=output.pwm=false; }
void board_kill(void) { stop_latch=true; output.enable=output.pwm=false; }
void board_release_stop(void) { stop_latch=false; }
void board_apply(const motor_output_t *out) {
    output=*out;
    if (PCB_STAGE==PCB_IO_CHECK || stop_pressed || stop_latch || external_fault!=FAULT_NONE)
        output.enable=output.pwm=false;
}
void board_adc_tick(void) {}
void board_watchdog_feed(void) {}
void board_service(void) {}
void board_leds(bool a,bool w,bool f,uint32_t n) { (void)a; (void)w; (void)f; (void)n; }
void board_inputs(motor_input_t *in,uint16_t raw[2]) {
    raw[0]=adc[0]; raw[1]=adc[1]; in->adc_ms=now-adc_delay; in->adc_valid=adc_good;
    in->driver_good=driver_good; in->hard_fault=external_fault!=FAULT_NONE;
    in->external_fault=external_fault;
}
static app_status_t status(void) { app_status_t s; app_status(&s); return s; }
static void ticks(unsigned n) { while(n--) { ++now; app_foreground(); app_control_tick(); } }
static void pot(float normalized_volts) { adc[0]=(uint16_t)(normalized_volts/3.0f*65535.0f); }
static void boot(bool held,float v) {
    now=adc_delay=0; arm_pressed=held; stop_pressed=false; adc_good=driver_good=true;
    stop_latch=true; external_fault=FAULT_NONE; adc[1]=12345; pot(v); app_init();
}
static void request(void) { arm_pressed=false; ticks(25); arm_pressed=true; ticks(25); }
static void arm(void) { pot(1.5f); request(); ticks(DEMO_OPEN_NEUTRAL_DWELL_MS+2u); }
int main(void) {
    boot(true,3); ticks(300); pot(1.5f); ticks(300);
    assert(status().motor.state==MOTOR_DISABLED && !output.enable);
    request(); ticks(DEMO_OPEN_NEUTRAL_DWELL_MS+2u);
    if (PCB_STAGE==PCB_IO_CHECK) {
        pot(3); ticks(5000);
        assert(status().motor.state==MOTOR_DISABLED && !output.enable && !output.pwm);
        assert(status().adc_raw[1]==12345 && status().command_volts==3.0f);
        puts("PASS: IO profile cannot arm or request a bridge, both ADC diagnostics available."); return 0;
    }
    assert(status().motor.state==MOTOR_OPEN_IDLE);
    pot(2.35f); ticks(2500); assert(output.pwm && status().motor.open_speed>0);
    /* VR2 cannot affect the motor command. */
    adc[1]=65535; ticks(1); assert(status().motor.open_target_speed>0);
    pot(0.65f); bool coast=false,reverse=false;
    for (unsigned i=0;i<8000;++i) {
        ticks(1);
        if (status().motor.state==MOTOR_OPEN_COAST) { coast=true; assert(!output.enable); }
        if (status().motor.state==MOTOR_OPEN_RUNNING && status().motor.open_speed<0) { reverse=true; break; }
    }
    assert(coast && reverse);
    /* ISR STOP inhibits immediately, independent of the next app tick. */
    app_stop(); assert(!output.enable && !output.pwm); ticks(1);
    assert(status().motor.state==MOTOR_DISABLED); pot(1.5f); ticks(300);
    assert(status().motor.state==MOTOR_DISABLED); /* Held ARM cannot restart. */
    /* Request outside center waits; stale/invalid data restarts full dwell. */
    pot(3); request(); assert(status().waiting_neutral && !output.enable);
    pot(1.5f); ticks(100); adc_good=false; ticks(200);
    adc_good=true; ticks(100); adc_delay=DEMO_ADC_MAX_AGE_MS+1; ticks(30);
    adc_delay=0; ticks(DEMO_OPEN_NEUTRAL_DWELL_MS-1u);
    assert(status().motor.state==MOTOR_DISABLED);
    ticks(2); assert(status().motor.state==MOTOR_OPEN_IDLE);
    /* STOP during wake/configuration, and held STOP, dominate ARM. */
    pot(3); ticks(1); assert(status().motor.state==MOTOR_OPEN_WAKE);
    stop_pressed=true; ticks(1); assert(!output.pwm && !output.enable);
    request(); stop_pressed=false; ticks(500); assert(status().motor.state==MOTOR_DISABLED);
    /* A press overlapping STOP for less than the debounce interval is ignored. */
    arm_pressed=false; ticks(25); stop_pressed=true; arm_pressed=true; ticks(1);
    stop_pressed=false; pot(1.5f); ticks(500); assert(status().motor.state==MOTOR_DISABLED);
    arm(); pot(2.0f); ticks(40); assert(status().motor.state==MOTOR_OPEN_ALIGN);
    stop_pressed=true; ticks(1); assert(status().motor.state==MOTOR_DISABLED && !output.pwm);
    stop_pressed=false; arm(); pot(2.0f); ticks(2500);
    driver_good=false; ticks(1); assert(status().motor.state==MOTOR_FAULT && !output.pwm);
    driver_good=true; arm(); assert(status().motor.state==MOTOR_FAULT);
    boot(false,1.5f); arm(); adc_delay=DEMO_ADC_MAX_AGE_MS+1; ticks(1);
    assert(status().motor.state==MOTOR_FAULT && status().motor.fault==FAULT_ADC);
    boot(false,3); request(); pot(1.5f); ticks(60);
    for (unsigned i=0;i<150;++i) { ++now; app_control_tick(); }
    assert(status().motor.state==MOTOR_DISABLED && status().waiting_neutral);
    ticks(DEMO_OPEN_NEUTRAL_DWELL_MS-1u); assert(status().motor.state==MOTOR_DISABLED);
    ticks(2); assert(status().motor.state==MOTOR_OPEN_IDLE);
    boot(false,3); request(); board_trip(FAULT_DRIVER_CONFIG); ticks(1);
    assert(status().motor.state==MOTOR_FAULT && !status().waiting_neutral && !output.pwm);
    boot(false,1.5f); now=UINT32_MAX-40; app_init(); arm();
    assert(status().motor.state==MOTOR_OPEN_IDLE);
    puts("PASS: actual app boot-held inhibit, momentary ARM/STOP priority, neutral dwell, VR1 isolation, +/- ramp/reversal, wake cancellation, stale ADC/foreground, fault latch, time wrap.");
}
