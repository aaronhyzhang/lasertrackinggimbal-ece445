/* Actual application and controller: buttons/board boundary are scripted. */
#include "demo_app.c"
#include <assert.h>
#include <stdlib.h>
unsigned board_active_axis;volatile unsigned pcb_current_axis;
void board_select_axis(unsigned i){board_active_axis=i;}
static uint32_t us,kills,writes;
static uint32_t mask,trip_fault;
static bool interrupt_on_clock_read;
static bool arm,stop,enabled,pwm;
static uint32_t simulated_work_us;
uint32_t HAL_GetTick(void) { return us/1000; }
uint32_t board_us(void) {
    uint32_t sampled=us;
    if (interrupt_on_clock_read && !mask) {
        interrupt_on_clock_read=false;us+=1000;dual_last_tick=us;
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
void pcb_control_read_axis(unsigned axis,control_input_t *in) {(void)axis;pcb_control_read(in);}
void board_current_pwm(const float duty[3]) { (void)duty;++writes;us+=simulated_work_us; }
void board_apply(const motor_output_t *out) { enabled=out->enable;pwm=out->pwm; }
void board_adc_tick(void) {}
void board_service(void) {}
void board_watchdog_feed(void) {}
void board_leds(bool a,bool b,bool c,uint32_t n) { (void)a;(void)b;(void)c;(void)n; }
void network_poll(void) {}
const char *network_status(void) { return "TEST"; }

static void tick(void){us+=1000;dual_foreground_ms=HAL_GetTick();app_control_tick();}
int main(void){
    us=1000;app_init();
    assert(dual[0].servo.config.velocity_ki==18.795f && dual[1].servo.config.velocity_ki==42.3f);
    for(unsigned i=0;i<2;i++) {
        position_control_t *s=&dual[i].servo;s->state=SERVO_READY;s->commutation_calibrated=s->zero_set=true;
        s->unwrap_started=true;s->raw_turns=.5f;s->previous_raw=2048;s->previous_encoder_us=us;
        s->zero_turns=.5f+(i?1:-1)*(170.0f/360);s->position_deg=s->trajectory_deg=170;
        app_select_axis(i);assert(app_position(SOURCE_ETHERNET,(175+i*5)*1000));
    }
    for(unsigned j=0;j<25;j++)tick();arm=true;
    for(unsigned j=0;j<400;j++) {
        if(j%100==0)for(unsigned i=0;i<2;i++){app_select_axis(i);assert(app_heartbeat(SOURCE_ETHERNET));}
        tick();
    }
    assert(dual[0].servo.state==SERVO_RUNNING && dual[1].servo.state==SERVO_RUNNING);
    for(unsigned i=0;i<2;i++){pcb_current_axis=i;app_current_sample();}
    assert(writes==2);
    app_select_axis(1);assert(!app_position(SOURCE_ETHERNET,338000));
    app_stop();tick();assert(dual[0].servo.state==SERVO_READY && dual[1].servo.state==SERVO_READY && !pwm);
    dual[1].servo.state=SERVO_FAULT;dual[1].servo.fault=SERVO_CURRENT;
    tick();assert(!pwm && !enabled);
    /* Reproduce the observed valid 158-us-old dual frame without weakening
     * acquisition-validity or enforcing the single-axis 150-us threshold. */
    control_input_t in=dual_inputs(1);position_control_t s;position_init_axis(&s,1);
    assert(CONTROL_CURRENT_SAMPLE_US==100u && CONTROL_CURRENT_MAX_AGE_US==300u);
    s.state=SERVO_WAKE_CAL;s.stage_ms=in.now_ms-350u;in.current_us=in.now_us-158u;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.state==SERVO_ALIGN);
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.state==SERVO_ALIGN);
    control_output_t out;position_fast(&s,&in,&out);assert(out.pwm && s.state==SERVO_ALIGN);
    in.current_us=in.now_us-300u;position_fast(&s,&in,&out);assert(out.pwm);
    in.current_us=in.now_us-301u;position_fast(&s,&in,&out);
    assert(!out.pwm && s.fault==SERVO_FEEDBACK && s.trip_input.current_us==in.current_us);
    position_init_axis(&s,1);s.state=SERVO_ALIGN;in.current_us=in.now_us-301u;
    position_tick(&s,&in,SERVO_NO_ACTION);assert(s.fault==SERVO_FEEDBACK);
    position_init_axis(&s,1);s.state=SERVO_ALIGN;in.current_us=in.now_us;in.current_valid=false;
    position_fast(&s,&in,&out);assert(s.fault==SERVO_FEEDBACK && !out.pwm);
    puts("PASS dual 158-us frame, wake/fast/slow consistency, 300-us boundary, stale and invalid-frame faults");
    puts("PASS independent gains, simultaneous S1 arm, both fast paths, pan bounds and shared stop/fault");
}
