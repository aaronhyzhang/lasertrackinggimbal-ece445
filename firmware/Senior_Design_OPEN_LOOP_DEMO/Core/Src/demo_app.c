/* Physical RUN and neutral qualification around the 1 kHz motor step. */
#include "demo_app.h"
#include "board.h"
#include "demo_config.h"
#include "run_switch.h"
#include <string.h>

static motor_t motor;
static run_switch_t run_switch;
static volatile bool neutral_wait, stop_pending;
static bool neutral_tracking;
static uint32_t neutral_since, last_tick_us, last_feed;
static volatile uint32_t foreground_ms;
static app_status_t status;

void app_init(void) {
    motor_init(&motor);
    memset(&run_switch, 0, sizeof(run_switch));
    memset(&status, 0, sizeof(status));
    status.motor = motor;
    neutral_wait = neutral_tracking = stop_pending = false;
    neutral_since = 0;
    foreground_ms = last_feed = HAL_GetTick();
    last_tick_us = board_us();
}

void app_stop(void) {
    uint32_t mask = board_lock();
    stop_pending = true;
    board_kill();
    board_unlock(mask);
}

void app_control_tick(void) {
    uint32_t start = board_us(), elapsed = start - last_tick_us;
    last_tick_us = start;
    if (status.ticks > 0 && (elapsed < 700u || elapsed > 1500u))
        board_trip(FAULT_CONTROL_TIMING);

    motor_input_t in = {0};
    motor_output_t out;
    in.now_ms = HAL_GetTick();
    board_inputs(&in, &status.adc_raw);
    uint32_t mask = board_lock();
    in.stop = stop_pending;
    stop_pending = false;
    in.foreground_ms = foreground_ms;
    board_unlock(mask);
    in.analog_volts = (float)status.adc_raw * (DEMO_ADC_VREF / 65535.0f);
    if (in.analog_volts > 3.15f) in.adc_valid = false;

    motor_action_t action = ACTION_NONE;
    bool run_high = board_run_high();
    motor_action_t physical = run_switch_step(&run_switch, run_high, in.now_ms);
    if (in.stop || motor.state == MOTOR_FAULT || in.hard_fault) {
        neutral_wait = false;
        neutral_tracking = false;
    }
    if (physical == ACTION_DISABLE) {
        neutral_wait = false;
        neutral_tracking = false;
        in.stop = true;
        action = ACTION_DISABLE;
        board_kill();
    } else if (physical == ACTION_ARM && !in.stop &&
               motor.state == MOTOR_DISABLED && !in.hard_fault) {
        neutral_wait = true;
        neutral_tracking = false;
    }
    if (neutral_wait && run_high && !in.stop &&
        motor.state == MOTOR_DISABLED && !in.hard_fault) {
        bool valid_center = in.adc_valid &&
            in.now_ms - in.adc_ms <= DEMO_ADC_MAX_AGE_MS &&
            in.now_ms - in.foreground_ms <= DEMO_FOREGROUND_MAX_AGE_MS &&
            motor_open_neutral(in.analog_volts);
        if (!valid_center) neutral_tracking = false;
        else {
            if (!neutral_tracking) {
                neutral_tracking = true;
                neutral_since = in.now_ms;
            }
            if (in.now_ms - neutral_since >= DEMO_OPEN_NEUTRAL_DWELL_MS) {
                neutral_wait = neutral_tracking = false;
                action = ACTION_ARM;
                board_release_stop();
            }
        }
    }
    motor_step(&motor, &in, action, &out);
    board_apply(&out);
    board_adc_tick();
    ++status.ticks;
    uint32_t used = board_us() - start;
    if (used > status.max_control_us) status.max_control_us = used;
    if (used > 500u) board_trip(FAULT_CONTROL_TIMING);
    status.motor = motor;
    status.waiting_neutral = neutral_wait;
    status.driver_good = in.driver_good;
    status.adc_age_ms = in.now_ms - in.adc_ms;
}

void app_foreground(void) {
    uint32_t now = HAL_GetTick();
    foreground_ms = now;
    if (now - last_feed >= 20u) {
        if (board_us() - last_tick_us < 20000u) board_watchdog_feed();
        last_feed = now;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14,
        motor.state == MOTOR_FAULT ? GPIO_PIN_SET : GPIO_PIN_RESET);
    bool armed = motor.state == MOTOR_OPEN_IDLE || motor.state == MOTOR_OPEN_WAKE ||
        motor.state == MOTOR_OPEN_ALIGN || motor.state == MOTOR_OPEN_RUNNING ||
        motor.state == MOTOR_OPEN_COAST;
    bool waiting = neutral_wait && motor.state == MOTOR_DISABLED;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1,
        (armed || (waiting && ((now / 250u) & 1u))) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void app_status(app_status_t *snapshot) {
    uint32_t mask = board_lock();
    *snapshot = status;
    board_unlock(mask);
}
