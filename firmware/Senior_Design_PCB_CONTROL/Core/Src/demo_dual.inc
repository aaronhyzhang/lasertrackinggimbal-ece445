/* Dual-axis voltage-mode supervisor. Shared S1 arms all queued positions;
 * S2, DISABLE, shared nFAULT and either controller fault inhibit BOTH bridges. */
#include "motor_profiles.h"
#include "system_fsm.h"
#if CONTROL_CURRENT_PI_ENABLED
#error Dual-axis bring-up supports voltage commutation only
#endif
typedef struct {
    position_control_t servo;
    bool valid,calibrate,calibrating,zero;
    float target;
    uint32_t command_ms,revision;
} dual_axis_t;
static dual_axis_t dual[2];
volatile app_status_t g_app_status;
volatile app_status_t g_axis_status[2];
static unsigned command_axis;
static arm_button_t dual_arm;
static volatile bool dual_stop;
static uint32_t dual_foreground_ms,dual_last_tick,dual_last_feed;
/* Gimbal-level state (docs/SYSTEM_FSM.md): observes both servos, may only stop/inhibit. */
system_fsm_t g_system;
static struct { bool seen,detected; uint32_t ms; } dual_target;
void app_select_axis(unsigned axis) { if(axis<2) command_axis=axis; }
static bool dual_active(const position_control_t *s) {
    return s->state==SERVO_WAKE_CAL || s->state==SERVO_WAKE_RUN || position_driving(s);
}
static void dual_publish(unsigned i,const control_input_t *in) {
    app_status_t status={0};status.servo=dual[i].servo;status.source=SOURCE_ETHERNET;
    status.test_axis=(uint8_t)i;status.motion_enabled=CONTROL_MOTION_ENABLED;
    status.driver_good=in->driver_good;status.ticks=g_axis_status[i].ticks+1;
    status.max_control_us=g_axis_status[i].max_control_us;
    status.command_age_ms=in->now_ms-in->command_ms;
    status.foreground_age_ms=in->now_ms-dual_foreground_ms;
    g_axis_status[i]=status;
    if(i==PCB_TEST_AXIS) g_app_status=status;
}
void app_init(void) {
    memset(dual,0,sizeof(dual));memset(&dual_arm,0,sizeof(dual_arm));
    for(unsigned i=0;i<2;i++) { position_init_axis(&dual[i].servo,i);dual[i].revision=1; }
    dual_foreground_ms=dual_last_feed=HAL_GetTick();dual_last_tick=board_us();
    command_axis=PCB_TEST_AXIS;dual_stop=false;
    system_fsm_init(&g_system,NULL,HAL_GetTick());memset(&dual_target,0,sizeof(dual_target));
}
void app_stop(void) {
    uint32_t m=board_lock();dual_stop=true;board_kill();
    for(unsigned i=0;i<2;i++) dual[i].valid=dual[i].calibrate=dual[i].calibrating=dual[i].zero=false;
    board_unlock(m);
}
static control_input_t dual_inputs(unsigned i) {
    board_select_axis(i);control_input_t in={0};pcb_control_read_axis(i,&in);
    motor_input_t b={0};uint16_t pots[2];board_inputs(&b,pots);
    in.driver_good=b.driver_good;in.hard_fault=b.hard_fault;
    in.hard_fault_reason=b.external_fault==FAULT_CONTROL_TIMING?SERVO_TIMING:SERVO_DRIVER;
    in.zero_ready=b.current_zero_ready;in.current_valid=in.current_valid && in.zero_ready;
    in.target_deg=dual[i].target;in.command_valid=dual[i].valid;in.command_ms=dual[i].command_ms;
    if(in.now_ms-dual_foreground_ms>DEMO_FOREGROUND_MAX_AGE_MS) {
        in.hard_fault=true;in.hard_fault_reason=SERVO_FOREGROUND;
    }
    return in;
}
void app_current_sample(void) {
    unsigned i=pcb_current_axis;uint32_t m=board_lock(),saved=board_active_axis,start=board_us();
    if(dual_stop || board_stop_pressed()) { board_kill();board_unlock(m);return; }
    position_control_t *s=&dual[i].servo;
    if(position_driving(s)) {
        control_input_t in=dual_inputs(i);control_output_t out;position_fast(s,&in,&out);
        if(s->state==SERVO_FAULT) { board_kill();dual_stop=true; }
        else if(out.pwm) board_current_pwm(out.duty);
        uint32_t used=board_us()-start;if(used>s->max_fast_us)s->max_fast_us=used;
        if(used>45u && s->state!=SERVO_FAULT) { position_trip(s,&in,SERVO_TIMING);board_kill();dual_stop=true; }
    }
    board_select_axis(saved);board_unlock(m);
}
void app_control_tick(void) {
    uint32_t m=board_lock(),saved=board_active_axis,start=board_us(),elapsed=start-dual_last_tick;
    dual_last_tick=start;bool stop=dual_stop || board_stop_pressed();
    bool arm=arm_button_step(&dual_arm,board_arm_pressed(),HAL_GetTick());
    if(stop) { board_kill();dual_arm.released=false; }
    for(unsigned i=0;i<2;i++) {
        dual_axis_t *d=&dual[i];control_input_t in=dual_inputs(i);servo_action_t action=SERVO_NO_ACTION;
        if(stop) { action=SERVO_STOP;d->valid=d->calibrating=d->calibrate=d->zero=false; }
        else if(d->calibrate) { d->calibrate=false;d->calibrating=true;action=SERVO_CALIBRATE;board_release_stop(); }
        else if(d->zero) { d->zero=false;action=SERVO_SET_ZERO; }
        else if(arm && d->valid && d->servo.state==SERVO_READY) { action=SERVO_ARM;board_release_stop(); }
        if(d->calibrating && in.now_ms-d->command_ms>CONTROL_COMMAND_MAX_AGE_MS)
            position_trip(&d->servo,&in,SERVO_COMMAND_TIMEOUT);
        if(g_axis_status[i].ticks && (elapsed<700 || elapsed>1500)) position_trip(&d->servo,&in,SERVO_TIMING);
        position_tick(&d->servo,&in,action);
        if(d->calibrating && (d->servo.state==SERVO_READY || d->servo.state==SERVO_FAULT))
            d->valid=d->calibrating=false;
        if(d->servo.state==SERVO_FAULT) { board_kill();dual_stop=true;stop=true; }
        motor_output_t out={0};out.enable=dual_active(&d->servo);out.pwm=position_driving(&d->servo);
        for(unsigned j=0;j<3;j++)out.duty[j]=.5f;
        board_apply(&out);dual_publish(i,&in);
    }
    system_input_t si={0};system_output_t so;
    si.now_ms=HAL_GetTick();si.init_done=true; /* app_init() runs after board and network setup. */
    for(unsigned i=0;i<2;i++) {
        si.axis_state[i]=dual[i].servo.state;si.axis_fault[i]=dual[i].servo.fault;
        si.axis_calibrated[i]=dual[i].servo.commutation_calibrated && dual[i].servo.zero_set;
    }
    si.target_seen=dual_target.seen;si.target_detected=dual_target.detected;si.target_ms=dual_target.ms;
    system_fsm_step(&g_system,&si,&so);
    if(so.stop)stop=true; /* FAULT latch or search timeout: same inhibit path as S2/DISABLE. */
    if(stop) { board_kill();for(unsigned i=0;i<2;i++) { position_stop(&dual[i].servo);dual[i].valid=false;g_axis_status[i].servo=dual[i].servo; } }
    dual_stop=false;
    uint32_t used=board_us()-start;
    for(unsigned i=0;i<2;i++)if(used>g_axis_status[i].max_control_us)g_axis_status[i].max_control_us=used;
    board_select_axis(saved);board_unlock(m);board_adc_tick();
}
void app_foreground(void) {
    uint32_t now=HAL_GetTick();dual_foreground_ms=now;board_service();network_poll();
    if(now-dual_last_feed>=20) {
        uint32_t m=board_lock(),age=board_us()-dual_last_tick;board_unlock(m);
        if(age<20000) board_watchdog_feed();else board_trip(FAULT_CONTROL_TIMING);
        dual_last_feed=now;
    }
    bool run=false,ready=false,fault=false;
    for(unsigned i=0;i<2;i++) { run|=position_driving(&dual[i].servo);ready|=dual[i].servo.state==SERVO_READY;fault|=dual[i].servo.state==SERVO_FAULT; }
    board_leds(run,ready,fault,now);
}
void app_status(app_status_t *s) { uint32_t m=board_lock();*s=g_axis_status[command_axis];s->servo=dual[command_axis].servo;board_unlock(m); }
bool app_position(command_source_t source,uint32_t deg) {
    uint32_t m=board_lock();dual_axis_t *d=&dual[command_axis];
    bool ok=source==SOURCE_ETHERNET && CONTROL_MOTION_ENABLED && deg<=(command_axis?337000u:360000u) &&
        (d->servo.state==SERVO_READY || d->servo.state==SERVO_RUNNING || d->servo.state==SERVO_WAKE_RUN) && d->servo.zero_set && !board_stop_pressed() && g_system.state!=SYS_FAULT;
    if(d->servo.state==SERVO_RUNNING && HAL_GetTick()-d->command_ms>500)ok=false;
    if(ok) { d->target=deg/1000.0f;d->command_ms=HAL_GetTick();d->valid=true; }
    board_unlock(m);return ok;
}
bool app_heartbeat(command_source_t source) {
    uint32_t m=board_lock();dual_axis_t *d=&dual[command_axis];
    bool ok=source==SOURCE_ETHERNET && d->valid && d->servo.state!=SERVO_FAULT && HAL_GetTick()-d->command_ms<=500;
    if(ok) { d->command_ms=HAL_GetTick(); }
    board_unlock(m);return ok;
}
bool app_target(bool detected) {
    uint32_t m=board_lock();
    dual_target.seen=true;dual_target.detected=detected;dual_target.ms=HAL_GetTick();
    board_unlock(m);return true;
}
bool app_calibrate(command_source_t source) {
    uint32_t m=board_lock();dual_axis_t *d=&dual[command_axis],*other=&dual[1-command_axis];
    bool ok=source==SOURCE_ETHERNET && CONTROL_MOTION_ENABLED && !board_stop_pressed() &&
        !dual_active(&other->servo) && !other->calibrate && !other->valid &&
        (d->servo.state==SERVO_OFF || d->servo.state==SERVO_READY) && g_system.state!=SYS_FAULT;
    if(ok) { d->calibrate=true;d->valid=true;d->command_ms=HAL_GetTick(); }
    board_unlock(m);return ok;
}
bool app_zero(command_source_t source) {
    uint32_t m=board_lock();dual_axis_t *d=&dual[command_axis];
    bool ok=source==SOURCE_ETHERNET && !board_stop_pressed() && d->servo.state==SERVO_READY && d->servo.commutation_calibrated;
    if(ok) { d->zero=true;d->valid=false; }
    board_unlock(m);return ok;
}
bool app_set_parameter(uint32_t key,uint32_t value) {
    uint32_t m=board_lock();dual_axis_t *d=&dual[command_axis];position_config_t c=d->servo.config;
    bool ok=(d->servo.state==SERVO_OFF || d->servo.state==SERVO_READY) && key<6;
    float v=value/1000.0f;
    switch(key) {case 0:c.velocity_kp=v;break;case 1:c.velocity_ki=v;break;case 2:c.position_kp=v;break;
    case 3:c.max_rpm=v;break;case 4:c.accel_rpm_s=v;break;case 5:c.run_volts=v;break;default:ok=false;}
    ok=ok && position_config_valid(&c);if(ok){d->servo.config=c;++d->revision;}
    board_unlock(m);return ok;
}
void app_status_text(char *text,size_t length) {
    app_status_t s;app_status(&s);position_control_t *v=&s.servo;
    uint32_t m=board_lock();unsigned sys=g_system.state,reason=g_system.reason;board_unlock(m);
    snprintf(text,length,"S1 axis=%u source=1 enabled=%u state=%u fault=%u calibrated=%u zero=%u position_mdeg=%ld target_mdeg=%ld rpm_milli=%ld iq_ma=%ld current_age_us=%lu encoder_age_us=%lu settled=%u weak=%u regulation=voltage network=%s dual=1 phase_trip_enabled=%u sys=%u sys_reason=%u",
        command_axis,CONTROL_MOTION_ENABLED,v->state,v->fault,v->commutation_calibrated,v->zero_set,
        (long)lroundf(v->position_deg*1000),(long)lroundf(v->target_deg*1000),(long)lroundf(v->velocity_rpm*1000),
        (long)lroundf(v->iq_a*1000),(unsigned long)v->current_age_us,(unsigned long)v->encoder_age_us,
        v->settled,v->weak_magnet,network_status(),CONTROL_SOFTWARE_PHASE_TRIP_ENABLED,sys,reason);
}
void app_config_text(char *text,size_t length) {
    uint32_t m=board_lock();position_config_t c=dual[command_axis].servo.config;uint32_t rev=dual[command_axis].revision;board_unlock(m);
    uint32_t w[8]={(uint32_t)lroundf(c.velocity_kp*1000),(uint32_t)lroundf(c.velocity_ki*1000),(uint32_t)lroundf(c.position_kp*1000),
        (uint32_t)lroundf(c.max_rpm*1000),(uint32_t)lroundf(c.accel_rpm_s*1000),(uint32_t)lroundf(c.run_volts*1000),250,6000};
    uint32_t hash=2166136261u;for(unsigned i=0;i<8;i++)for(unsigned b=0;b<4;b++){hash^=(w[i]>>(b*8))&255;hash*=16777619u;}
    snprintf(text,length,"C1 schema=1 build=dual-20261027 revision=%lu checksum=%lu axis=%u source=1 current_pi=0 velocity_kp=%lu velocity_ki=%lu position_kp=%lu max_rpm=%lu accel_rpm_s=%lu run_volts=%lu duty_span=250 voltage_limit=6000 min_mdeg=2000 max_mdeg=%u encoder_sign=%d dual=1 small_move_mdeg=%u small_accel_mrpm_s=%u",
        (unsigned long)rev,(unsigned long)hash,command_axis,(unsigned long)w[0],(unsigned long)w[1],(unsigned long)w[2],(unsigned long)w[3],(unsigned long)w[4],(unsigned long)w[5],command_axis?337000:358000,command_axis?-1:0,command_axis?20000:0,command_axis?114066:150000);
}
bool app_test_prepare(const uint32_t a[6]) {(void)a;return false;}
void app_test_text(char *text,size_t length) {snprintf(text,length,"T1 dual=1 supported=0 prepared=0 running=0 complete=0 aborted=0");}
size_t app_trace_read(uint32_t first,void *bytes,size_t length) {(void)first;(void)bytes;(void)length;return 0;}
