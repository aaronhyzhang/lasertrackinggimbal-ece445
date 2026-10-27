#include "demo_app.h"
#include "board.h"
#include "demo_config.h"
#include "pcb_config.h"
#include "control_config.h"
#include "demo_network.h"
#include "arm_button.h"
#include "commission.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#if CONTROL_DUAL_AXIS
#include "demo_dual.inc"
#else
static position_control_t servo;
static arm_button_t arm_button;
static volatile bool stop_pending,command_valid;
static volatile bool calibration_pending,remote_calibration;
static volatile bool zero_pending;
static volatile uint32_t command_ms;
static volatile float command_deg;
static volatile uint32_t foreground_ms,last_tick_us;
static uint32_t last_feed,stop_since;
static bool stop_previous,stop_eligible;
volatile app_status_t g_app_status;
void app_init(void) {
    position_init(&servo);memset(&arm_button,0,sizeof(arm_button));
    commission_init();
    stop_pending=command_valid=false;command_ms=0;command_deg=0;
    calibration_pending=remote_calibration=false;
    zero_pending=false;
    foreground_ms=last_feed=HAL_GetTick();last_tick_us=board_us();
    app_status_t empty={0};g_app_status=empty;
    g_app_status.test_axis=PCB_TEST_AXIS;g_app_status.motion_enabled=CONTROL_MOTION_ENABLED;
    g_app_status.current_regulation=CONTROL_CURRENT_PI_ENABLED;
    g_app_status.source=CONTROL_SOURCE_ETHERNET?SOURCE_ETHERNET:SOURCE_ANALOG;
}
void app_stop(void) {
    uint32_t m=board_lock();stop_pending=true;command_valid=false;
    commission_abort();
    calibration_pending=remote_calibration=zero_pending=false;board_kill();board_unlock(m);
}
static control_input_t inputs(void) {
    control_input_t in={0};pcb_control_read(&in);
    motor_input_t b={0};uint16_t adc[2];board_inputs(&b,adc);
    in.driver_good=b.driver_good;in.hard_fault=b.hard_fault;
    g_app_status.board_fault=b.external_fault;
    if (b.hard_fault) {
        if (b.external_fault==FAULT_CONTROL_TIMING) in.hard_fault_reason=SERVO_TIMING;
        else if (b.external_fault==FAULT_DRIVER || b.external_fault==FAULT_DRIVER_CONFIG) in.hard_fault_reason=SERVO_DRIVER;
        else if (b.external_fault==FAULT_ADC || b.external_fault==FAULT_CURRENT_CALIBRATION) in.hard_fault_reason=SERVO_FEEDBACK;
        else in.hard_fault_reason=SERVO_INTERNAL;
    }
    in.zero_ready=b.current_zero_ready;in.current_valid=in.current_valid && in.zero_ready;
    if (CONTROL_SOURCE_ETHERNET) {
        in.target_deg=command_deg;in.command_valid=command_valid;in.command_ms=command_ms;
    } else {
        /* Full pot travel is ratiometric 0..3.3 V -> requested 0..360 degrees.
         * The applied target retains a two-degree endpoint margin. */
        in.target_deg=(float)adc[0]*(360.0f/65535.0f);
        in.command_valid=b.adc_valid && in.now_ms-b.adc_ms<=DEMO_ADC_MAX_AGE_MS;
        in.command_ms=b.adc_ms;
    }
    g_app_status.foreground_age_ms=in.now_ms-foreground_ms;
    if (!in.hard_fault && g_app_status.foreground_age_ms>DEMO_FOREGROUND_MAX_AGE_MS) {
        in.hard_fault=true;in.hard_fault_reason=SERVO_FOREGROUND;
    }
    return in;
}
void app_current_sample(void) {
    uint32_t start=board_us(),m=board_lock();
    if (stop_pending || board_stop_pressed()) { board_kill();board_unlock(m);return; }
    /* Inhibited/waking states only need the fresh frame read by the 1 kHz
     * supervisor. Do not run a 20 kHz PWM-control workload with all gates off. */
    if (!position_driving(&servo)) { board_unlock(m);return; }
    control_input_t in=inputs();control_output_t out;
    position_fast(&servo,&in,&out);
    if (servo.state==SERVO_FAULT) board_kill();
    else if (out.pwm) board_current_pwm(out.duty);
    uint32_t used=board_us()-start;
    if (used>servo.max_fast_us) servo.max_fast_us=used;
    /* Voltage commutation has no current PI half-period deadline. Retain
     * headroom before the next 50-us trigger so an actual overrun still trips. */
    uint32_t budget=CONTROL_CURRENT_PI_ENABLED?25u:45u;
    if (used>budget && servo.state!=SERVO_FAULT) { position_trip(&servo,&in,SERVO_TIMING);board_kill(); }
    board_unlock(m);
}
void app_control_tick(void) {
    uint32_t start=board_us(),elapsed=start-last_tick_us;last_tick_us=start;
    uint32_t m=board_lock();
    control_input_t in=inputs();servo_action_t action=SERVO_NO_ACTION;
    bool stop=board_stop_pressed();
    if ((g_commission.prepared || g_commission.running) &&
        in.now_ms-command_ms>CONTROL_COMMAND_MAX_AGE_MS) {
        position_trip(&servo,&in,SERVO_COMMAND_TIMEOUT);commission_abort();board_kill();
    } else if (g_commission.authorized && !g_commission.prepared && !g_commission.running &&
               in.now_ms-command_ms>CONTROL_COMMAND_MAX_AGE_MS) {
        g_commission.authorized=false;command_valid=false;
    }
    if (zero_pending) {
        zero_pending=false;
        if (!stop && !stop_pending && servo.state==SERVO_READY && servo.commutation_calibrated)
            action=SERVO_SET_ZERO;
    }
    if (calibration_pending) {
        calibration_pending=false;
        if (!stop && !stop_pending && (servo.state==SERVO_OFF || servo.state==SERVO_READY)) {
            action=SERVO_CALIBRATE;board_release_stop();
        } else { remote_calibration=false;command_valid=false; }
    }
    if (remote_calibration && in.now_ms-command_ms>CONTROL_COMMAND_MAX_AGE_MS) {
        position_trip(&servo,&in,SERVO_COMMAND_TIMEOUT);board_kill();
        remote_calibration=command_valid=false;
    }
    bool edge=arm_button_step(&arm_button,board_arm_pressed(),in.now_ms);
    if (stop && !stop_previous) {
        stop_since=in.now_ms;
        stop_eligible=servo.state==SERVO_OFF || servo.state==SERVO_READY;
    }
    if (!stop && stop_previous && stop_eligible && in.now_ms-stop_since>=CONTROL_LONG_PRESS_MS) {
        action=servo.commutation_calibrated?SERVO_SET_ZERO:SERVO_CALIBRATE;
        board_release_stop();
    }
    if (stop || stop_pending) {
        action=SERVO_STOP;stop_pending=false;
        arm_button.released=false;board_kill();
    } else if (edge && action==SERVO_NO_ACTION) { action=SERVO_ARM;board_release_stop(); }
    stop_previous=stop;
    if (g_commission.prepared && action==SERVO_ARM) g_commission.authorized=true;
    if (g_commission.prepared && g_commission.authorized && servo.state==SERVO_READY &&
        action==SERVO_NO_ACTION && !stop && !stop_pending) {
        action=SERVO_ARM;board_release_stop();
    }
    if (servo.state!=SERVO_FAULT && g_app_status.ticks && (elapsed<700u || elapsed>1500u)) {
        position_trip(&servo,&in,SERVO_TIMING);board_kill();
    }
    bool finished=commission_input(&in,&servo);
    if (g_commission.aborted && g_commission.authorized) {
        position_trip(&servo,&in,SERVO_TRAVEL);commission_abort();
    }
    if (finished) {
        position_stop(&servo);command_valid=false;command_ms=in.now_ms;
        in.command_valid=false;
    }
    position_tick(&servo,&in,action);
    if (servo.state==SERVO_FAULT) commission_abort();
    commission_capture(&in,&servo);
    if (remote_calibration && (servo.state==SERVO_READY || servo.state==SERVO_FAULT)) {
        remote_calibration=command_valid=false;
    }
    motor_output_t out={0};
    out.enable=servo.state==SERVO_WAKE_CAL || servo.state==SERVO_WAKE_RUN || position_driving(&servo);
    out.pwm=position_driving(&servo);
    for (unsigned j=0;j<3;++j) out.duty[j]=0.5f; /* Only initial neutral start. */
    if (servo.state==SERVO_FAULT) board_kill();
    board_apply(&out);
    g_app_status.servo=servo;g_app_status.driver_good=in.driver_good;
    g_app_status.command_age_ms=in.now_ms-in.command_ms;
    g_app_status.calibration_button_held=stop && stop_eligible && in.now_ms-stop_since>=CONTROL_LONG_PRESS_MS;
    ++g_app_status.ticks;
    uint32_t used=board_us()-start;
    if (used>g_app_status.max_control_us) g_app_status.max_control_us=used;
    board_unlock(m);
    board_adc_tick();
}
void app_foreground(void) {
    uint32_t now=HAL_GetTick();foreground_ms=now;
    board_service();network_poll();
    if (now-last_feed>=20u) {
        /* TIM7 may preempt foreground between either operand of subtraction.
         * Snapshot both under the same lock: a newer last_tick_us must never
         * be subtracted from an earlier board_us() sample (unsigned underflow). */
        uint32_t mask=board_lock(),tick=last_tick_us,current=board_us();
        uint32_t age=current-tick;g_app_status.control_age_us=age;board_unlock(mask);
        if (age<20000u) board_watchdog_feed();
        else board_trip(FAULT_CONTROL_TIMING);
        last_feed=now;
    }
    app_status_t s;app_status(&s);
    board_leds(position_driving(&s.servo),s.servo.state==SERVO_READY,s.servo.state==SERVO_FAULT,now);
}
void app_status(app_status_t *s) { uint32_t m=board_lock();*s=g_app_status;board_unlock(m); }
bool app_position(command_source_t source,uint32_t mdeg) {
    uint32_t m=board_lock();
    bool ok=CONTROL_SOURCE_ETHERNET && source==SOURCE_ETHERNET && (float)mdeg<=CONTROL_ACCEPT_MAX_DEG*1000 &&
        !g_commission.prepared && !g_commission.running &&
        (servo.state==SERVO_READY || servo.state==SERVO_RUNNING || servo.state==SERVO_WAKE_RUN) && servo.zero_set;
    if (servo.state==SERVO_RUNNING && HAL_GetTick()-command_ms>CONTROL_COMMAND_MAX_AGE_MS) ok=false;
    if (ok) { g_commission.authorized=false;command_deg=(float)mdeg/1000;command_ms=HAL_GetTick();command_valid=true; }
    board_unlock(m);return ok;
}
bool app_calibrate(command_source_t source) {
    uint32_t m=board_lock();
    bool ok=CONTROL_SOURCE_ETHERNET && CONTROL_MOTION_ENABLED && source==SOURCE_ETHERNET &&
        !stop_pending && !board_stop_pressed() && !calibration_pending && !remote_calibration &&
        !g_commission.prepared && !g_commission.running &&
        (servo.state==SERVO_OFF || servo.state==SERVO_READY);
    if (ok) {
        commission_abort();
        calibration_pending=remote_calibration=true;command_valid=true;
        command_ms=HAL_GetTick();command_deg=0;
    }
    board_unlock(m);return ok;
}
bool app_zero(command_source_t source) {
    uint32_t m=board_lock();
    bool ok=CONTROL_SOURCE_ETHERNET && CONTROL_MOTION_ENABLED && source==SOURCE_ETHERNET &&
        servo.state==SERVO_READY && servo.commutation_calibrated &&
        !g_commission.prepared && !g_commission.running &&
        !stop_pending && !board_stop_pressed() && !calibration_pending && !remote_calibration && !zero_pending;
    if (ok) { commission_abort();zero_pending=true;command_valid=false; }
    board_unlock(m);return ok;
}
bool app_heartbeat(command_source_t source) {
    uint32_t m=board_lock();bool ok=CONTROL_SOURCE_ETHERNET && source==SOURCE_ETHERNET &&
        (command_valid || g_commission.authorized) &&
        HAL_GetTick()-command_ms<=CONTROL_COMMAND_MAX_AGE_MS && servo.state!=SERVO_FAULT;
    if (ok) command_ms=HAL_GetTick();
    board_unlock(m);return ok;
}
bool app_target(bool detected) { (void)detected;return false; }
bool app_set_parameter(uint32_t key,uint32_t value) {
    uint32_t m=board_lock();position_config_t c=servo.config;
    bool ok=CONTROL_SOURCE_ETHERNET && !CONTROL_CURRENT_PI_ENABLED &&
        (servo.state==SERVO_OFF || servo.state==SERVO_READY) &&
        !g_commission.prepared && !g_commission.running && key<6;
    float v=(float)value/1000;
    switch(key) {
    case 0:c.velocity_kp=v;break;case 1:c.velocity_ki=v;break;case 2:c.position_kp=v;break;
    case 3:c.max_rpm=v;break;case 4:c.accel_rpm_s=v;break;case 5:c.run_volts=v;break;default:ok=false;
    }
    ok=ok && position_config_valid(&c);
    if (ok) {
        servo.config=c;servo.velocity_integral=servo.id_integral=servo.iq_integral=0;
        ++g_commission.revision;g_app_status.servo=servo;
    }
    board_unlock(m);return ok;
}
bool app_test_prepare(const uint32_t a[6]) {
    uint32_t m=board_lock();
    bool ok=CONTROL_SOURCE_ETHERNET && CONTROL_MOTION_ENABLED && !CONTROL_CURRENT_PI_ENABLED &&
        servo.state==SERVO_READY && servo.commutation_calibrated && servo.zero_set &&
        !stop_pending && !board_stop_pressed() && !remote_calibration && !calibration_pending &&
        (float)a[4]-(float)a[5]>=CONTROL_COMMAND_MIN_DEG*1000 &&
        (float)a[4]+(float)a[5]<=CONTROL_COMMAND_MAX_DEG*1000 &&
        fabsf(servo.position_deg-(float)a[4]/1000)<=(float)a[5]/1000;
    control_input_t in=inputs();
    if (a[0]==1 || a[0]==3) ok=ok && (float)a[1]/1000<=position_voltage_limit(&servo,in.bus_v);
    if (a[0]==2) ok=ok && (float)a[1]/1000<=servo.config.max_rpm;
    if (ok) ok=commission_prepare(a[0],a[1],a[2],a[3],a[4],a[5]);
    if (ok) { command_deg=(float)a[4]/1000;command_ms=HAL_GetTick();command_valid=true; }
    board_unlock(m);return ok;
}
void app_config_text(char *text,size_t length) {
    uint32_t m=board_lock();position_config_t c=servo.config;uint32_t rev=g_commission.revision;
    board_unlock(m);
    uint32_t words[8]={(uint32_t)lroundf(c.velocity_kp*1000),(uint32_t)lroundf(c.velocity_ki*1000),
        (uint32_t)lroundf(c.position_kp*1000),(uint32_t)lroundf(c.max_rpm*1000),
        (uint32_t)lroundf(c.accel_rpm_s*1000),(uint32_t)lroundf(c.run_volts*1000),
        (uint32_t)lroundf(position_duty_span()*1000),(uint32_t)lroundf(CONTROL_VOLT_LIMIT*1000)};
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<8;++i) for(unsigned byte=0;byte<4;++byte) { hash^=(words[i]>>(8*byte))&255u;hash*=16777619u; }
    snprintf(text,length,"C1 schema=1 build=commission-20261027 revision=%lu checksum=%lu axis=%u source=%u current_pi=%u "
        "velocity_kp=%ld velocity_ki=%ld position_kp=%ld max_rpm=%ld accel_rpm_s=%ld run_volts=%ld "
        "duty_span=%ld voltage_limit=%ld min_mdeg=%ld max_mdeg=%ld encoder_sign=%d",
        (unsigned long)rev,(unsigned long)hash,PCB_TEST_AXIS,CONTROL_SOURCE_ETHERNET,CONTROL_CURRENT_PI_ENABLED,
        (long)lroundf(c.velocity_kp*1000),(long)lroundf(c.velocity_ki*1000),(long)lroundf(c.position_kp*1000),
        (long)lroundf(c.max_rpm*1000),(long)lroundf(c.accel_rpm_s*1000),(long)lroundf(c.run_volts*1000),
        (long)lroundf(position_duty_span()*1000),(long)lroundf(CONTROL_VOLT_LIMIT*1000),
        (long)lroundf(CONTROL_COMMAND_MIN_DEG*1000),(long)lroundf(CONTROL_COMMAND_MAX_DEG*1000),
        CONTROL_ENCODER_POSITIVE_SIGN);
}
void app_test_text(char *text,size_t length) {
    uint32_t m=board_lock();commission_state_t c=g_commission;board_unlock(m);
    snprintf(text,length,"T1 trial=%lu mode=%lu prepared=%u running=%u complete=%u aborted=%u authorized=%u next=%lu revision=%lu",
        (unsigned long)c.trial,(unsigned long)c.mode,c.prepared,c.running,c.complete,c.aborted,c.authorized,
        (unsigned long)c.next_sequence,(unsigned long)c.revision);
}
size_t app_trace_read(uint32_t first,void *bytes,size_t length) {
    uint32_t m=board_lock();size_t used=commission_read(first,bytes,length);board_unlock(m);return used;
}
void app_status_text(char *text,size_t length) {
    app_status_t s;app_status(&s);position_control_t *p=&s.servo;
    /* Integer wire units avoid printf float support and ambiguous NaN handling. */
    snprintf(text,length,"S1 axis=%u source=%u enabled=%u state=%u fault=%u calibrated=%u zero=%u "
        "position_mdeg=%ld target_mdeg=%ld rpm_milli=%ld iq_ma=%ld current_age_us=%lu encoder_age_us=%lu "
        "settled=%u weak=%u regulation=%s network=%s",s.test_axis,s.source,s.motion_enabled,p->state,p->fault,
        p->commutation_calibrated,p->zero_set,(long)lroundf(p->position_deg*1000),
        (long)lroundf(p->target_deg*1000),(long)lroundf(p->velocity_rpm*1000),(long)lroundf(p->iq_a*1000),
        (unsigned long)p->current_age_us,(unsigned long)p->encoder_age_us,p->settled,p->weak_magnet,
        CONTROL_CURRENT_PI_ENABLED?"current":"voltage",network_status());
}

#endif
