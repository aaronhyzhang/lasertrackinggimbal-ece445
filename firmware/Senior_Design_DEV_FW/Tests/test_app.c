#include "demo_app.h"
#include "demo_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t now;
static float shaft=1;
static motor_fault_t external_fault;
static bool stop_latch=true;
static motor_output_t output;
static bool encoder_present=true;
static uint16_t analog_raw;
uint32_t board_lock(void) { return 0; }
void board_unlock(uint32_t mask) { (void)mask; }
uint32_t HAL_GetTick(void) { return now; }
uint32_t board_us(void) { return now*1000u; }
void board_trip(motor_fault_t f) { external_fault=f; output.enable=output.pwm=false; }
void board_clear_trip(void) { external_fault=FAULT_NONE; }
void board_kill(void) { stop_latch=true; output.enable=output.pwm=false; }
void board_release_stop(void) { stop_latch=false; }
void board_apply(const motor_output_t *out) {
    output=*out; if (stop_latch || external_fault!=FAULT_NONE) output.enable=output.pwm=false;
}
void board_sensor_tick(void) {}
void board_watchdog_feed(void) {}
void HAL_GPIO_WritePin(void *port,uint32_t pin,int value) { (void)port; (void)pin; (void)value; }
const char *network_status(void) { return "HOST_FAKE"; }
void board_inputs(motor_input_t *in,uint16_t *adc) {
    in->encoder_ms=in->health_ms=in->adc_ms=now;
    in->raw_angle=(uint16_t)(motor_wrap(shaft)*(4096.0f/6.28318530718f));
    in->encoder_valid=in->magnet_good=encoder_present;
    in->adc_valid=in->driver_good=true;
    in->hard_fault=external_fault!=FAULT_NONE; in->external_fault=external_fault; *adc=analog_raw;
}
static void tick(void) { ++now; app_foreground(); app_control_tick(); }
static void test_open_application(void) {
    now=0; external_fault=FAULT_NONE; stop_latch=true; encoder_present=false; analog_raw=0;
    app_init(); tick();
    assert(!app_mode((motor_mode_t)99));
    assert(app_source(SOURCE_SERIAL)); assert(app_position(SOURCE_SERIAL,90000));
    assert(app_mode(MODE_OPEN_LOOP)); assert(!app_request(ACTION_ARM)); tick();
    app_status_t s; app_status(&s);
    assert(s.motor.mode==MODE_OPEN_LOOP && s.motor.state==MOTOR_DISABLED && s.source==SOURCE_ANALOG);
    assert(!app_heartbeat(SOURCE_SERIAL)); assert(!app_position(SOURCE_ANALOG,10000));
    assert(!app_source(SOURCE_ETHERNET)); assert(!app_request(ACTION_CALIBRATE)); assert(!app_request(ACTION_ZERO));
    analog_raw=59577; assert(app_request(ACTION_ARM)); tick();
    app_status(&s); assert(s.motor.state==MOTOR_DISABLED && !output.pwm); /* ARM at 3 V is refused. */
    analog_raw=0; assert(app_request(ACTION_ARM)); tick();
    app_status(&s); assert(s.motor.state==MOTOR_OPEN_IDLE && !output.pwm);
    assert(!app_mode(MODE_CLOSED_LOOP)); assert(!app_source(SOURCE_ANALOG));
    analog_raw=29789;
    for (unsigned i=0;i<2500;++i) tick();
    app_status(&s); assert(s.motor.state==MOTOR_OPEN_RUNNING && output.pwm);
    char text[640]; size_t length=app_status_text(text,sizeof(text));
    assert(length<sizeof(text)-1 && strstr(text,"mode=OPEN_LOOP") && strstr(text,"pos_valid=0"));
    assert(strstr(text,"cmd_mrpm=") && strstr(text,"open_mV="));
    app_stop(); assert(!output.pwm); assert(!app_mode(MODE_CLOSED_LOOP)); tick();
    assert(app_mode(MODE_CLOSED_LOOP)); app_stop(); tick();
    app_status(&s); assert(s.motor.mode==MODE_OPEN_LOOP); /* Stop cancels a pending mode change. */
    assert(app_mode(MODE_CLOSED_LOOP)); tick();
    app_status(&s); assert(s.motor.mode==MODE_CLOSED_LOOP && s.motor.state==MOTOR_DISABLED && !s.motor.zeroed);
    assert(!app_request(ACTION_ARM)); assert(app_request(ACTION_CALIBRATE)); tick();
    app_status(&s); assert(s.motor.state==MOTOR_DISABLED && !output.pwm); /* Closed loop still requires encoder. */
}
int main(void) {
    app_init(); tick();
    assert(!app_request(ACTION_ARM)); assert(app_request(ACTION_CALIBRATE));
    assert(!app_request(ACTION_CALIBRATE)); tick();
    app_status_t s;
    for (unsigned i=0;i<7500;++i) {
        app_status(&s); if (s.motor.state==MOTOR_READY) break;
        uint32_t t=now-s.motor.stage_ms;
        if (s.motor.state==MOTOR_CAL_SWEEP) shaft=1+(1.57079632679f/11)*fminf(t/1500.0f,1);
        if (s.motor.state==MOTOR_CAL_RETURN) shaft=1+(1.57079632679f/11)*(1-fminf(t/1500.0f,1));
        tick();
    }
    app_status(&s); assert(s.motor.state==MOTOR_READY && !output.pwm);
    assert(app_request(ACTION_ZERO)); tick(); assert(app_request(ACTION_ARM));
    app_stop(); assert(!output.pwm); tick();
    app_status(&s); assert(s.motor.state==MOTOR_READY && !output.enable);
    assert(app_source(SOURCE_ETHERNET)); assert(!app_heartbeat(SOURCE_ETHERNET));
    assert(!app_position(SOURCE_ETHERNET,360001)); assert(!app_position(SOURCE_SERIAL,0));
    assert(app_position(SOURCE_ETHERNET,90000)); assert(app_heartbeat(SOURCE_ETHERNET));
    assert(app_request(ACTION_ARM));
    for (unsigned i=0;i<15;++i) tick();
    app_status(&s); assert(s.motor.state==MOTOR_RUNNING && output.pwm);
    assert(!app_source(SOURCE_ANALOG));
    for (unsigned i=0;i<505;++i) {
        app_status(&s); shaft=1+s.motor.reference; tick();
    }
    app_status(&s); assert(s.motor.state==MOTOR_FAULT && s.motor.fault==FAULT_COMMAND);
    assert(!output.enable && !app_heartbeat(SOURCE_ETHERNET));
    assert(app_request(ACTION_CLEAR_FAULT)); tick();
    app_status(&s); assert(s.motor.state==MOTOR_DISABLED && !s.motor.calibrated && !s.motor.zeroed);
    assert(!app_heartbeat(SOURCE_ETHERNET)); assert(!app_request(ACTION_ARM));
    test_open_application();
    puts("PASS: application mailbox/stop, source/lease ownership, pending-mode cancellation, zero-speed ARM,\n"
         "analog-only open loop without encoder, status units and closed-loop recalibration interlocks.");
    return 0;
}
