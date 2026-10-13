#include "demo_app.h"
#include "board.h"
#include "demo_config.h"
#include "run_switch.h"
#include "demo_network.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static motor_t motor;
#if DEMO_BOARD_OPEN_ACTIVE
static run_switch_t run_switch;
#endif
static volatile motor_action_t pending;
static volatile bool mode_pending;
static motor_mode_t pending_mode;
static volatile bool stop_pending;
static volatile command_source_t source;
static volatile uint32_t command_ms, target_mdeg, foreground_ms;
static volatile bool target_valid;
static volatile uint32_t ticks, max_control_us;
static uint32_t last_tick_us;
static uint32_t last_feed;
static app_status_t status;
void app_init(void) {
#if DEMO_BOARD_OPEN_ACTIVE
    memset(&run_switch,0,sizeof(run_switch));
#endif
    motor_init(&motor); source=SOURCE_ANALOG; foreground_ms=HAL_GetTick();
    pending=ACTION_NONE; mode_pending=false; stop_pending=false;
    target_valid=false; target_mdeg=0; command_ms=HAL_GetTick()-DEMO_COMMAND_MAX_AGE_MS-1u;
    ticks=0; max_control_us=0; last_feed=HAL_GetTick();
    memset(&status,0,sizeof(status)); status.motor=motor; status.source=source;
    last_tick_us=board_us();
}
void app_stop(void) {
    uint32_t mask=board_lock(); pending=ACTION_NONE; mode_pending=false; stop_pending=true;
    board_kill(); board_unlock(mask);
}
bool app_request(motor_action_t action) {
    if (action==ACTION_DISABLE) { app_stop(); return true; }
    uint32_t mask=board_lock();
#if DEMO_BOARD_OPEN_ACTIVE
    board_unlock(mask); return false; /* Physical RUN owns ARM/restart authority. */
#endif
    bool accepted=pending==ACTION_NONE && !stop_pending && !mode_pending && action!=ACTION_NONE;
    if (action==ACTION_CALIBRATE) accepted &= motor.mode==MODE_CLOSED_LOOP && motor_inhibited(&motor);
    if (action==ACTION_ZERO) accepted &= motor.mode==MODE_CLOSED_LOOP && motor.state==MOTOR_READY;
    if (action==ACTION_ARM) {
        if (motor.mode==MODE_OPEN_LOOP) accepted &= motor.state==MOTOR_DISABLED && source==SOURCE_ANALOG;
        else accepted &= motor.state==MOTOR_READY && motor.zeroed;
    }
    if (action==ACTION_CLEAR_FAULT) accepted &= motor.state==MOTOR_FAULT;
    if (accepted) pending=action;
    board_unlock(mask); return accepted;
}
bool app_mode(motor_mode_t new_mode) {
#if DEMO_BOARD_OPEN_ACTIVE
    (void)new_mode; return false; /* Reflash to change board-control profile. */
#endif
    uint32_t mask=board_lock();
    bool accepted=(new_mode==MODE_OPEN_LOOP || new_mode==MODE_CLOSED_LOOP) &&
        motor_inhibited(&motor) && pending==ACTION_NONE && !mode_pending && !stop_pending;
    if (accepted) {
        pending_mode=new_mode; mode_pending=true;
        target_valid=false; target_mdeg=0;
        command_ms=HAL_GetTick()-DEMO_COMMAND_MAX_AGE_MS-1u;
        board_kill(); /* No mode request can authorize gates. */
    }
    board_unlock(mask); return accepted;
}
bool app_source(command_source_t new_source) {
    uint32_t mask=board_lock();
    bool accepted=(new_source==SOURCE_ANALOG || new_source==SOURCE_SERIAL || new_source==SOURCE_ETHERNET) &&
        motor_inhibited(&motor) && pending==ACTION_NONE && !mode_pending && !stop_pending &&
        (motor.mode==MODE_CLOSED_LOOP || new_source==SOURCE_ANALOG);
    if (accepted) { source=new_source; command_ms=HAL_GetTick()-DEMO_COMMAND_MAX_AGE_MS-1u; target_mdeg=0; target_valid=false; }
    board_unlock(mask); return accepted;
}
bool app_position(command_source_t owner,uint32_t value) {
    if (value>360000u) return false;
    uint32_t mask=board_lock(); bool accepted=source==owner && motor.mode==MODE_CLOSED_LOOP && !mode_pending;
    if (accepted) { target_mdeg=value; command_ms=HAL_GetTick(); target_valid=true; }
    board_unlock(mask); return accepted;
}
bool app_heartbeat(command_source_t owner) {
    uint32_t mask=board_lock(); bool accepted=source==owner && target_valid && motor.mode==MODE_CLOSED_LOOP && !mode_pending;
    if (accepted) command_ms=HAL_GetTick();
    board_unlock(mask); return accepted;
}
void app_control_tick(void) {
    uint32_t start=board_us(), elapsed=start-last_tick_us; last_tick_us=start;
    /* Measured period rather than assuming an interrupted loop remained 1 kHz. */
    if (ticks>0 && (elapsed<700u || elapsed>1500u)) board_trip(FAULT_CONTROL_TIMING);
    motor_input_t in={0}; motor_output_t out;
    in.now_ms=HAL_GetTick(); board_inputs(&in,&status.adc_raw);
    uint32_t mask=board_lock();
    motor_action_t action=pending; pending=ACTION_NONE;
    in.stop=stop_pending; stop_pending=false;
    if (mode_pending) {
        if (!in.stop) { (void)motor_select_mode(&motor,pending_mode); source=SOURCE_ANALOG; }
        mode_pending=false;
    }
    in.source=source; in.command_ms=command_ms; in.foreground_ms=foreground_ms;
    in.target_rad=(float)target_mdeg*(6.28318530718f/360000.0f);
    if (action==ACTION_CLEAR_FAULT) {
        board_clear_trip(); in.hard_fault=false; target_valid=false; target_mdeg=0;
        command_ms=in.now_ms-DEMO_COMMAND_MAX_AGE_MS-1u;
    }
    if (!in.stop && (action==ACTION_CALIBRATE || action==ACTION_ARM)) board_release_stop();
    board_unlock(mask);
    if (in.source==SOURCE_ANALOG) {
        float voltage=(float)status.adc_raw*(DEMO_ADC_VREF/65535.0f);
        in.analog_volts=voltage;
        /* An above-range source is a fault, not an unchecked 396-degree target. */
        if (voltage>3.15f) in.adc_valid=false;
        in.target_rad=fminf(voltage/3.0f,1.0f)*6.28318530718f;
    }
#if DEMO_BOARD_OPEN_ACTIVE
    motor_action_t physical=run_switch_step(&run_switch,board_run_high(),in.now_ms);
    if (physical==ACTION_DISABLE) {
        in.stop=true; action=ACTION_DISABLE; board_kill();
    } else if (physical==ACTION_ARM && !in.stop && motor.state==MOTOR_DISABLED &&
               !in.hard_fault && in.adc_valid && in.now_ms-in.adc_ms<=DEMO_ADC_MAX_AGE_MS &&
               in.now_ms-in.foreground_ms<=DEMO_FOREGROUND_MAX_AGE_MS &&
               in.analog_volts<=DEMO_OPEN_ARM_MAX_VOLTS) {
        action=ACTION_ARM; board_release_stop();
    }
#endif
    motor_state_t previous=motor.state;
    motor_step(&motor,&in,action,&out); board_apply(&out);
    if (previous!=MOTOR_FAULT && motor.state==MOTOR_FAULT) {
        uint32_t m=board_lock(); target_valid=false; target_mdeg=0;
        command_ms=in.now_ms-DEMO_COMMAND_MAX_AGE_MS-1u; board_unlock(m);
    }
    board_sensor_tick();
    ++ticks;
    uint32_t used=board_us()-start;
    if (used>max_control_us) max_control_us=used;
    if (used>500u) board_trip(FAULT_CONTROL_TIMING);
    status.motor=motor; status.source=source; status.encoder_raw=in.raw_angle;
    status.magnet_good=in.magnet_good; status.driver_good=in.driver_good;
    status.ticks=ticks; status.max_control_us=max_control_us; status.command_ms=command_ms;
    status.encoder_age_ms=in.now_ms-in.encoder_ms; status.health_age_ms=in.now_ms-in.health_ms;
    status.adc_age_ms=in.now_ms-in.adc_ms;
}
void app_foreground(void) {
    uint32_t now=HAL_GetTick(); foreground_ms=now;
    if (now-last_feed>=20u) {
        if (board_us()-last_tick_us<20000u) board_watchdog_feed();
        last_feed=now;
    }
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_14,motor.state==MOTOR_FAULT?GPIO_PIN_SET:GPIO_PIN_RESET);
#if DEMO_BOARD_OPEN_ACTIVE
    bool armed=motor.state==MOTOR_OPEN_IDLE || motor.state==MOTOR_OPEN_WAKE ||
               motor.state==MOTOR_OPEN_ALIGN || motor.state==MOTOR_OPEN_RUNNING;
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_1,armed?GPIO_PIN_SET:GPIO_PIN_RESET);
#endif
}
void app_status(app_status_t *s) { uint32_t mask=board_lock(); *s=status; board_unlock(mask); }
size_t app_status_text(char *text,size_t capacity) {
    if (!capacity) return 0;
    app_status_t s; app_status(&s);
    int n=snprintf(text,capacity,
        "state=%s fault=%s source=%s mode=%s cal=%u zero=%u pos_mdeg=%ld target_mdeg=%ld uq_mV=%ld "
        "raw=%u adc=%u magnet=%u nfault=%u ticks=%lu max_us=%lu bus_config_mV=24000 "
        "dir=%d vel_mdeg_s=%ld ref_mdeg=%ld enc_age_ms=%lu health_age_ms=%lu adc_age_ms=%lu eth=%s fw=%s "
        "pos_valid=%u cmd_mrpm=%ld ramp_mrpm=%ld open_mV=%ld\r\n",
        motor_state_name(s.motor.state),motor_fault_name(s.motor.fault),
        s.source==SOURCE_ANALOG?"ANALOG":s.source==SOURCE_SERIAL?"SERIAL":"ETHERNET",
        motor_mode_name(s.motor.mode),
        s.motor.calibrated,s.motor.zeroed,(long)(s.motor.position*57295.7795f),
        (long)(s.motor.target*57295.7795f),(long)(s.motor.effort*1000.0f),
        s.encoder_raw,s.adc_raw,s.magnet_good,s.driver_good,
        (unsigned long)s.ticks,(unsigned long)s.max_control_us,
        s.motor.direction,(long)(s.motor.velocity*57295.7795f),(long)(s.motor.reference*57295.7795f),
        (unsigned long)s.encoder_age_ms,(unsigned long)s.health_age_ms,(unsigned long)s.adc_age_ms,
        network_status(),DEMO_FW_VERSION,
        (unsigned)(s.motor.mode==MODE_CLOSED_LOOP && s.motor.zeroed && s.motor.calibrated && s.magnet_good &&
                   s.encoder_age_ms<=DEMO_ENCODER_MAX_AGE_MS && s.health_age_ms<=DEMO_HEALTH_MAX_AGE_MS),
        (long)(s.motor.open_target_speed*9549.296586f),(long)(s.motor.open_speed*9549.296586f),
        (long)(s.motor.open_volts*1000.0f));
    return n<0 ? 0 : ((size_t)n<capacity ? (size_t)n : capacity-1u);
}
