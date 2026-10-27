#include "system_fsm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static system_fsm_t f;
static system_input_t in;
static system_output_t out;

static system_state_t step(uint32_t now) { in.now_ms=now; return system_fsm_step(&f,&in,&out); }
static void axes(servo_state_t a,servo_state_t b) { in.axis_state[0]=a; in.axis_state[1]=b; }
static void calibrated(bool v) { in.axis_calibrated[0]=in.axis_calibrated[1]=v; }
static void target(bool detected,uint32_t at) { in.target_seen=true; in.target_detected=detected; in.target_ms=at; }
static void reset(const system_config_t *c) {
    memset(&in,0,sizeof(in)); memset(&out,0,sizeof(out));
    system_fsm_init(&f,c,0);
}
/* Booted, calibrated, zeroed and both axes RUNNING. */
static void running(const system_config_t *c,uint32_t now) {
    reset(c); in.init_done=true; calibrated(true);
    axes(SERVO_READY,SERVO_READY); assert(step(now)==SYS_IDLE);
    axes(SERVO_RUNNING,SERVO_RUNNING); step(now);
}

static void test_boot_and_calibration(void) {
    reset(NULL);
    assert(step(0)==SYS_INIT && out.stop && !out.can_arm);
    assert(step(500)==SYS_INIT);
    in.init_done=true;
    assert(step(600)==SYS_IDLE && !out.stop && !out.can_arm);      /* not calibrated yet */
    axes(SERVO_WAKE_CAL,SERVO_OFF); assert(step(700)==SYS_CALIBRATING);
    axes(SERVO_SWEEP,SERVO_OFF); assert(step(800)==SYS_CALIBRATING);
    axes(SERVO_READY,SERVO_OFF); in.axis_calibrated[0]=true; assert(step(900)==SYS_IDLE && !out.can_arm);
    axes(SERVO_READY,SERVO_ALIGN); assert(step(950)==SYS_CALIBRATING);
    axes(SERVO_READY,SERVO_READY); assert(step(1000)==SYS_IDLE && !out.can_arm); /* axis 1 not zeroed */
    in.axis_calibrated[1]=true; assert(step(1100)==SYS_IDLE && out.can_arm);
    assert(f.previous==SYS_CALIBRATING && f.entered_ms==1000 && strcmp(system_state_name(f.state),"IDLE")==0);
}

static void test_arming_tracking_searching(void) {
    reset(NULL); in.init_done=true; calibrated(true); axes(SERVO_READY,SERVO_READY);
    assert(step(0)==SYS_IDLE && out.can_arm);
    axes(SERVO_WAKE_RUN,SERVO_WAKE_RUN); assert(step(10)==SYS_ARMING && !out.can_arm && !out.stop);
    axes(SERVO_RUNNING,SERVO_WAKE_RUN); assert(step(20)==SYS_ARMING);  /* waking wins */
    axes(SERVO_RUNNING,SERVO_RUNNING);
    assert(step(30)==SYS_SEARCHING && !out.stop);        /* no vision yet */
    target(true,40); assert(step(45)==SYS_TRACKING && !out.stop);
    target(false,60); assert(step(65)==SYS_SEARCHING && !out.stop);  /* ball lost */
    target(true,80); assert(step(90)==SYS_TRACKING);
    assert(step(80+300)==SYS_TRACKING);                                 /* exactly at stale limit */
    assert(step(80+301)==SYS_SEARCHING && !out.stop);    /* vision stalled */
}

static void test_stop_returns_idle(void) {
    running(NULL,100); target(true,100); assert(step(110)==SYS_TRACKING);
    axes(SERVO_READY,SERVO_READY);                    /* S2, DISABLE or lease expiry without fault */
    assert(step(120)==SYS_IDLE && !out.stop && out.can_arm);
}

static void test_search_timeout(void) {
    system_config_t c=system_default_config(); c.search_timeout_ms=2000;
    running(&c,0); assert(f.state==SYS_SEARCHING);
    assert(step(1999)==SYS_SEARCHING && !out.stop);
    assert(step(2000)==SYS_SEARCHING && out.stop && f.reason==SYS_REASON_SEARCH_TIMEOUT);
    target(true,2100); assert(step(2100)==SYS_TRACKING && !out.stop);   /* re-acquired before stop applied */
    target(false,2200); assert(step(2200)==SYS_SEARCHING && f.entered_ms==2200 && !out.stop); /* timer restarts */
    assert(step(4200)==SYS_SEARCHING && out.stop);
    axes(SERVO_READY,SERVO_READY); assert(step(4210)==SYS_IDLE && !out.stop);

    running(NULL,0); assert(step(1000000)==SYS_SEARCHING && !out.stop); /* default: hold forever */
}

static void test_estop(void) {
    running(NULL,0); target(true,0); assert(step(5)==SYS_TRACKING && !out.stop);
    in.estop=true;                                     /* while tracking: stop at once and latch */
    assert(step(6)==SYS_FAULT && out.stop && f.reason==SYS_REASON_ESTOP && !out.can_arm);
    in.estop=false; axes(SERVO_READY,SERVO_READY);     /* released: still latched until reset */
    assert(step(7)==SYS_FAULT && out.stop);
    system_config_t c=system_default_config(); c.search_timeout_ms=100;
    running(&c,0); assert(step(100)==SYS_SEARCHING && out.stop);
    in.estop=true; assert(step(110)==SYS_FAULT && out.stop && f.reason==SYS_REASON_ESTOP);
}

static void test_faults_latch(void) {
    running(NULL,0); target(true,0); assert(step(5)==SYS_TRACKING);
    axes(SERVO_RUNNING,SERVO_FAULT); in.axis_fault[1]=SERVO_FOLLOWING;
    assert(step(10)==SYS_FAULT && out.stop && !out.can_arm);
    assert(f.reason==SYS_REASON_AXIS_FAULT && f.fault_axis==1 && f.axis_fault==SERVO_FOLLOWING);
    axes(SERVO_READY,SERVO_READY); in.axis_fault[1]=SERVO_OK;     /* recovery does not clear it */
    assert(step(20)==SYS_FAULT && out.stop && f.axis_fault==SERVO_FOLLOWING);
    in.estop=true; assert(step(30)==SYS_FAULT && f.reason==SYS_REASON_AXIS_FAULT); /* first cause kept */

    running(NULL,0); in.board_fault=true;
    assert(step(10)==SYS_FAULT && f.reason==SYS_REASON_BOARD_FAULT);
    running(NULL,0); in.estop=true;
    assert(step(10)==SYS_FAULT && f.reason==SYS_REASON_ESTOP);

    reset(NULL); in.init_failed=true;                 /* fault during INIT */
    assert(step(0)==SYS_FAULT && f.reason==SYS_REASON_INIT_FAILED && out.stop);
    reset(NULL); axes(SERVO_FAULT,SERVO_OFF);          /* faulted axis before init completes */
    assert(step(0)==SYS_FAULT && f.fault_axis==0);
    reset(NULL); in.init_done=true; in.estop=true; axes(SERVO_FAULT,SERVO_OFF);
    assert(step(0)==SYS_FAULT && f.reason==SYS_REASON_ESTOP); /* e-stop reported first */
}

static void test_time_wrap(void) {
    uint32_t t=0xFFFFFF00u;
    reset(NULL); in.init_done=true; calibrated(true); axes(SERVO_RUNNING,SERVO_RUNNING);
    target(true,t); assert(step(t+10u)==SYS_TRACKING);
    assert(t+300u<t);                                  /* the clock wraps past zero below */
    assert(step(t+300u)==SYS_TRACKING);
    assert(step(t+301u)==SYS_SEARCHING);
}

int main(void) {
    test_boot_and_calibration();
    test_arming_tracking_searching();
    test_stop_returns_idle();
    test_search_timeout();
    test_estop();
    test_faults_latch();
    test_time_wrap();
    puts("system_fsm: all tests passed");
    return 0;
}
