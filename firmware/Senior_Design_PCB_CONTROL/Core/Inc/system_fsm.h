#ifndef SYSTEM_FSM_H
#define SYSTEM_FSM_H
/* Gimbal-level state machine above the two per-axis servos (design doc 1.1).
 * HAL-independent: the caller supplies a snapshot each control tick and applies
 * the outputs. It never commands motion itself; arming stays physical S1 and
 * each servo keeps its own protections. Its only action is a stop request.
 * FAULT latches until MCU reset, matching the servo fault policy. */
#include <stdbool.h>
#include <stdint.h>
#include "position_control.h"

#define SYSTEM_AXES 2u

typedef enum { SYS_INIT,SYS_CALIBRATING,SYS_IDLE,SYS_ARMING,
    SYS_TRACKING,SYS_SEARCHING,SYS_FAULT } system_state_t;
typedef enum { SYS_REASON_NONE,SYS_REASON_INIT_FAILED,SYS_REASON_AXIS_FAULT,
    SYS_REASON_BOARD_FAULT,SYS_REASON_ESTOP,SYS_REASON_SEARCH_TIMEOUT } system_reason_t;

typedef struct {
    uint32_t target_stale_ms;   /* Vision report older than this counts as lost. */
    uint32_t search_timeout_ms; /* 0 = hold in SEARCHING indefinitely. */
} system_config_t;

typedef struct {
    uint32_t now_ms;
    bool init_done,init_failed;        /* Board, drivers, encoders and network set up. */
    servo_state_t axis_state[SYSTEM_AXES];
    servo_fault_t axis_fault[SYSTEM_AXES];
    bool axis_calibrated[SYSTEM_AXES]; /* commutation_calibrated && zero_set */
    bool board_fault;                  /* nFAULT, timing/foreground or other board trip. */
    bool estop;                        /* Emergency-stop input asserted. */
    bool target_seen;                  /* Any vision report received since boot. */
    bool target_detected;              /* Latest vision report found the ball. */
    uint32_t target_ms;                /* Arrival time of that report. */
} system_input_t;

typedef struct {
    bool stop;      /* Inhibit both bridges now (caller: app_stop()/board_kill()). */
    bool can_arm;   /* Both axes calibrated, zeroed and READY. */
} system_output_t;

typedef struct {
    system_config_t config;
    system_state_t state,previous;
    system_reason_t reason;    /* Why FAULT was entered, or the last stop request. */
    servo_fault_t axis_fault;  /* First servo fault seen, for diagnostics. */
    uint8_t fault_axis;
    uint32_t entered_ms,transitions;
} system_fsm_t;

system_config_t system_default_config(void);
void system_fsm_init(system_fsm_t *f,const system_config_t *config,uint32_t now_ms);
system_state_t system_fsm_step(system_fsm_t *f,const system_input_t *in,system_output_t *out);
const char *system_state_name(system_state_t state);
#endif
