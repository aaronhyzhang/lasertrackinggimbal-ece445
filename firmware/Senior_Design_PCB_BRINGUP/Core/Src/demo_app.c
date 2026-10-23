/* Physical S1 ARM, S2 immediate STOP; 1 kHz open-loop control, no console. */
#include "demo_app.h"
#include "board.h"
#include "demo_config.h"
#include "pcb_config.h"
#include "arm_button.h"
#include <string.h>
static motor_t motor;
static arm_button_t arm_button;
static volatile bool neutral_wait,stop_pending;
static bool neutral_tracking;
static uint32_t neutral_since,last_feed;
static volatile uint32_t last_tick_us;
static volatile uint32_t foreground_ms;
volatile app_status_t g_app_status;

void app_init(void) {
    motor_init(&motor); memset(&arm_button,0,sizeof(arm_button));
    app_status_t empty={0}; g_app_status=empty;
    g_app_status.motor=motor; g_app_status.stage=PCB_STAGE;
    g_app_status.test_axis=PCB_TEST_AXIS;
    neutral_wait=neutral_tracking=stop_pending=false;
    neutral_since=0; foreground_ms=last_feed=HAL_GetTick(); last_tick_us=board_us();
}

void app_stop(void) {
    uint32_t mask=board_lock(); stop_pending=true; board_kill(); board_unlock(mask);
}

void app_control_tick(void) {
    uint32_t start=board_us(),elapsed=start-last_tick_us; last_tick_us=start;
    if (g_app_status.ticks>0 && (elapsed<700u || elapsed>1500u)) board_trip(FAULT_CONTROL_TIMING);
    motor_input_t in={0}; motor_output_t out={0}; uint16_t raw[2];
    in.now_ms=HAL_GetTick(); board_inputs(&in,raw);
    uint32_t mask=board_lock(); in.stop=stop_pending; stop_pending=false;
    in.foreground_ms=foreground_ms; board_unlock(mask);
    /* Onboard pot is ratiometric to 3V3_D; use full ADC travel as 0..3 command.
     * Existing 1.3..1.7 command neutral => nominal 1.43..1.87 V physical.
     * This assumes VDDA/VREF+ and the pot rail have the intended same nominal
     * voltage; verify raw endpoints/center in IO_CHECK before driving. */
    in.analog_volts=(float)raw[0]*(3.0f/65535.0f);
    bool arm=board_arm_pressed(),stop=board_stop_pressed();
    bool arm_edge=arm_button_step(&arm_button,arm,in.now_ms);
    motor_action_t action=ACTION_NONE;
    if (in.stop || stop || motor.state==MOTOR_FAULT || in.hard_fault) {
        neutral_wait=neutral_tracking=false;
    }
    if (stop || in.stop) {
        arm_button.released=false; /* A press overlapping STOP must be released. */
        in.stop=true; action=ACTION_DISABLE; board_kill();
    } else if (arm_edge && motor.state==MOTOR_DISABLED && !in.hard_fault && PCB_STAGE!=PCB_IO_CHECK) {
        neutral_wait=true; neutral_tracking=false;
    }
    if (neutral_wait && !in.stop && motor.state==MOTOR_DISABLED && !in.hard_fault) {
        bool center=in.adc_valid && in.now_ms-in.adc_ms<=DEMO_ADC_MAX_AGE_MS &&
            in.now_ms-in.foreground_ms<=DEMO_FOREGROUND_MAX_AGE_MS && motor_open_neutral(in.analog_volts);
        if (!center) neutral_tracking=false;
        else {
            if (!neutral_tracking) { neutral_tracking=true; neutral_since=in.now_ms; }
            if (in.now_ms-neutral_since>=DEMO_OPEN_NEUTRAL_DWELL_MS) {
                neutral_wait=neutral_tracking=false; action=ACTION_ARM; board_release_stop();
            }
        }
    }
    if (PCB_STAGE!=PCB_IO_CHECK) motor_step(&motor,&in,action,&out);
    else if (in.hard_fault) { motor.state=MOTOR_FAULT; motor.fault=in.external_fault; }
    board_apply(&out); board_adc_tick();
    g_app_status.adc_raw[0]=raw[0]; g_app_status.adc_raw[1]=raw[1];
    g_app_status.adc_valid=in.adc_valid; g_app_status.command_volts=in.analog_volts;
    g_app_status.arm_pressed=arm; g_app_status.stop_pressed=stop;
    g_app_status.waiting_neutral=neutral_wait; g_app_status.driver_good=in.driver_good;
    g_app_status.adc_age_ms=in.now_ms-in.adc_ms; ++g_app_status.ticks;
    uint32_t used=board_us()-start;
    if (used>g_app_status.max_control_us) g_app_status.max_control_us=used;
    if (used>500u) board_trip(FAULT_CONTROL_TIMING);
    g_app_status.motor=motor;
}

void app_foreground(void) {
    uint32_t now=HAL_GetTick(); foreground_ms=now;
    board_service(); /* SPI runs here, never in the 1 kHz control interrupt. */
    if (now-last_feed>=20u) {
        if (board_us()-last_tick_us<20000u) board_watchdog_feed();
        else board_trip(FAULT_CONTROL_TIMING);
        last_feed=now;
    }
    app_status_t s; app_status(&s);
    bool armed=s.motor.state>=MOTOR_OPEN_IDLE && s.motor.state<=MOTOR_OPEN_COAST;
    board_leds(armed,s.waiting_neutral,s.motor.state==MOTOR_FAULT,now);
}

void app_status(app_status_t *snapshot) {
    uint32_t mask=board_lock(); *snapshot=g_app_status; board_unlock(mask);
}
