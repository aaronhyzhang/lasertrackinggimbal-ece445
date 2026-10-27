/* Actual application and controller: buttons/board boundary are scripted. */
#include "demo_app.c"
#include <assert.h>
#include <stdlib.h>
static uint32_t us,kills,writes;
static uint32_t mask,trip_fault;
static bool interrupt_on_clock_read;
static bool arm,stop,enabled,pwm;
static uint32_t simulated_work_us;
uint32_t HAL_GetTick(void) { return us/1000; }
uint32_t board_us(void) {
    uint32_t sampled=us;
    if (interrupt_on_clock_read && !mask) {
        interrupt_on_clock_read=false;us+=1000;last_tick_us=us;
    }
    return sampled;
}
uint32_t board_lock(void) { uint32_t previous=mask;mask=1;return previous; }
void board_unlock(uint32_t m) { mask=m; }
bool board_arm_pressed(void) { return arm; }
bool board_stop_pressed(void) { return stop; }
void board_kill(void) { ++kills;enabled=pwm=false; }
void board_release_stop(void) {}
void board_trip(motor_fault_t f) { trip_fault=f;board_kill(); }
void board_inputs(motor_input_t *in,uint16_t adc[2]) {
    adc[0]=32768;adc[1]=0;in->driver_good=in->adc_valid=in->current_zero_ready=true;
    in->adc_ms=HAL_GetTick();
}
void pcb_control_read(control_input_t *in) {
    in->now_us=in->encoder_us=in->current_us=us;in->now_ms=us/1000;
    in->current_sequence=us/50+1;in->encoder_raw=2048;in->encoder_status=0x33;
    in->encoder_transport=in->current_valid=in->zero_ready=in->analog_valid=in->temperature_valid=true;
    in->bus_v=24;in->temperature_c=25;
}
void board_current_pwm(const float duty[3]) { (void)duty;++writes;us+=simulated_work_us; }
void board_apply(const motor_output_t *out) { enabled=out->enable;pwm=out->pwm; }
void board_adc_tick(void) {}
void board_service(void) {}
void board_watchdog_feed(void) {}
void board_leds(bool a,bool b,bool c,uint32_t n) { (void)a;(void)b;(void)c;(void)n; }
void network_poll(void) {}
const char *network_status(void) { return "TEST"; }
static void tick(void) { us+=1000;foreground_ms=HAL_GetTick();app_control_tick(); }
static void reset(void) { us=1000;arm=stop=enabled=pwm=false;app_init();stop_previous=stop_eligible=false; }
int main(void) {
    if (CONTROL_SOURCE_ETHERNET && CONTROL_MOTION_ENABLED) {
        reset();servo.state=SERVO_READY;servo.commutation_calibrated=servo.zero_set=true;
        servo.unwrap_started=true;servo.raw_turns=0.5f;servo.position_deg=180;servo.previous_raw=2048;
        assert(app_set_parameter(0,650));assert(servo.config.velocity_kp==0.65f);
        uint32_t plan[6]={0,10000,6000,1000,180000,30000};
        assert(app_test_prepare(plan));
        for(unsigned j=0;j<25;++j)tick();
        assert(servo.state==SERVO_READY && !g_commission.authorized);
        arm=true;
        for(unsigned j=0;j<400;++j) { if(j%100==0)assert(app_heartbeat(SOURCE_ETHERNET));tick(); }
        assert(g_commission.running && g_commission.authorized);
        assert(!app_set_parameter(0,700)); /* No moving gain swap. */
        for(unsigned j=0;j<1100;++j) { if(j%100==0)assert(app_heartbeat(SOURCE_ETHERNET));tick(); }
        assert(g_commission.complete && servo.state==SERVO_READY && g_commission.authorized);
        assert(app_test_prepare(plan));tick();assert(servo.state==SERVO_WAKE_RUN); /* Same S1 batch. */
        app_stop();tick();assert(!g_commission.authorized && !g_commission.running);
        assert(servo.state==SERVO_READY);
    }
    reset();us=25000;last_tick_us=us;interrupt_on_clock_read=true;trip_fault=FAULT_NONE;
    app_foreground();
    assert(trip_fault==FAULT_NONE); /* A newer TIM7 stamp cannot underflow the age. */
    interrupt_on_clock_read=false;
    us=50000;last_tick_us=1000;last_feed=0;app_foreground();
    assert(trip_fault==FAULT_CONTROL_TIMING); /* Actual lost control tick still trips. */
    reset();
    servo.state=SERVO_ALIGN;servo.stage_ms=HAL_GetTick();simulated_work_us=26;
    app_current_sample();assert(servo.state==SERVO_ALIGN && servo.fault==SERVO_OK);
    simulated_work_us=46;app_current_sample();
    assert(servo.state==SERVO_FAULT && servo.fault==SERVO_TIMING);
    simulated_work_us=0;reset();
    bool remote_enabled=CONTROL_SOURCE_ETHERNET && CONTROL_MOTION_ENABLED;
    assert(app_calibrate(SOURCE_ETHERNET)==remote_enabled);
    if (remote_enabled) {
        tick();assert(servo.state==SERVO_WAKE_CAL && !servo.zero_set);
        assert(!app_calibrate(SOURCE_ETHERNET));
        for(unsigned j=0;j<510;++j) tick();
        assert(servo.state==SERVO_FAULT && servo.fault==SERVO_COMMAND_TIMEOUT);
        reset();assert(app_calibrate(SOURCE_ETHERNET));app_stop();tick();
        assert(servo.state==SERVO_OFF && !calibration_pending && !remote_calibration);
        reset();servo.state=SERVO_RUNNING;assert(!app_calibrate(SOURCE_ETHERNET));
        assert(!app_zero(SOURCE_ETHERNET));
        reset();assert(!app_zero(SOURCE_ETHERNET));
        servo.state=SERVO_READY;servo.commutation_calibrated=true;
        assert(app_zero(SOURCE_ETHERNET));assert(!servo.zero_set);
        tick();assert(servo.zero_set && servo.state==SERVO_READY && servo.position_deg==0 && !pwm);
        assert(app_zero(SOURCE_ETHERNET));app_stop();tick();assert(!zero_pending && !pwm);
    }
    reset();
    stop=true;app_stop();
    for(unsigned j=0;j<2100;++j) { tick();assert(!enabled && !pwm); }
    stop=false;tick();
    assert(servo.state==(CONTROL_MOTION_ENABLED?SERVO_WAKE_CAL:SERVO_OFF));
    reset();
    servo.commutation_calibrated=servo.zero_set=true;servo.state=SERVO_RUNNING;
    servo.zero_turns=123;servo.unwrap_started=true;servo.raw_turns=123;
    servo.previous_raw=2048;servo.previous_encoder_us=us;
    stop=true;app_stop();unsigned before=writes;app_current_sample();assert(writes==before && !enabled);
    for(unsigned j=0;j<2100;++j) { tick();assert(!enabled && !pwm); }
    stop=false;tick();
    assert(servo.state==SERVO_READY && servo.zero_turns==123); /* A running STOP cannot recalibrate. */
    reset();servo.commutation_calibrated=true;servo.state=SERVO_READY;
    arm=false;for(unsigned j=0;j<25;++j)tick();
    arm=true;for(unsigned j=0;j<25;++j)tick();assert(!enabled); /* No mechanical zero. */
    if(CONTROL_SOURCE_ETHERNET) {
        servo.zero_set=true;
        assert(app_position(SOURCE_ETHERNET,90000));
        assert(app_heartbeat(SOURCE_ETHERNET));
        us+=501000;assert(!app_heartbeat(SOURCE_ETHERNET));
        app_stop();assert(!app_heartbeat(SOURCE_ETHERNET));
        assert(!app_position(SOURCE_ETHERNET,360001));
    }
    puts("PASS actual S2 immediate inhibition, calibration only on stopped-release, no running re-zero, arming prerequisite and lease expiry");
}
