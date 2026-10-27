#include "system_fsm.h"
#include <string.h>

system_config_t system_default_config(void) {
    system_config_t c={0};
    c.target_stale_ms=300u; c.search_timeout_ms=0u;
    return c;
}
void system_fsm_init(system_fsm_t *f,const system_config_t *config,uint32_t now_ms) {
    memset(f,0,sizeof(*f));
    f->config=config?*config:system_default_config();
    f->state=f->previous=SYS_INIT; f->entered_ms=now_ms;
}
static void enter(system_fsm_t *f,system_state_t next,uint32_t now_ms) {
    if (next==f->state) return;
    f->previous=f->state; f->state=next; f->entered_ms=now_ms; ++f->transitions;
}
static bool calibrating(servo_state_t s) {
    return s==SERVO_WAKE_CAL || s==SERVO_ALIGN || s==SERVO_SWEEP || s==SERVO_RETURN;
}
/* Priority: latched fault, new fault, init, calibration, arming, running, idle. */
system_state_t system_fsm_step(system_fsm_t *f,const system_input_t *in,system_output_t *out) {
    uint32_t now=in->now_ms;
    bool any_cal=false,any_wake=false,any_run=false,all_ready=true;
    system_reason_t fault=SYS_REASON_NONE;
    for (unsigned i=0;i<SYSTEM_AXES;++i) {
        servo_state_t s=in->axis_state[i];
        any_cal|=calibrating(s); any_wake|=s==SERVO_WAKE_RUN; any_run|=s==SERVO_RUNNING;
        all_ready&=s==SERVO_READY && in->axis_calibrated[i];
        if (s==SERVO_FAULT && fault==SYS_REASON_NONE && f->state!=SYS_FAULT) {
            fault=SYS_REASON_AXIS_FAULT; f->fault_axis=(uint8_t)i; f->axis_fault=in->axis_fault[i];
        }
    }
    if (in->estop) fault=SYS_REASON_ESTOP;
    else if (in->init_failed) fault=SYS_REASON_INIT_FAILED;
    else if (in->board_fault && fault==SYS_REASON_NONE) fault=SYS_REASON_BOARD_FAULT;
    if (f->state!=SYS_FAULT && fault!=SYS_REASON_NONE) { f->reason=fault; enter(f,SYS_FAULT,now); }

    bool stop=false;
    if (f->state==SYS_FAULT) stop=true;
    else if (f->state==SYS_INIT && !in->init_done) stop=true;
    else if (any_cal) enter(f,SYS_CALIBRATING,now);
    else if (any_wake) enter(f,SYS_ARMING,now);
    else if (any_run) {
        bool fresh=in->target_seen && now-in->target_ms<=f->config.target_stale_ms;
        if (fresh && in->target_detected) enter(f,SYS_TRACKING,now);
        else enter(f,SYS_SEARCHING,now);  /* Hold last target; the laptop keeps the lease. */
        if (f->state==SYS_SEARCHING && f->config.search_timeout_ms &&
            now-f->entered_ms>=f->config.search_timeout_ms) {
            stop=true; f->reason=SYS_REASON_SEARCH_TIMEOUT;
        }
    } else enter(f,SYS_IDLE,now);

    out->stop=stop;
    out->can_arm=f->state==SYS_IDLE && all_ready;
    return f->state;
}
const char *system_state_name(system_state_t state) {
    static const char *const names[]={"INIT","CALIBRATING","IDLE","ARMING","TRACKING","SEARCHING","FAULT"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"?";
}
