#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "open_loop.h"
/* Inspect this snapshot in the debugger with the motor bus disconnected. */
typedef struct {
    motor_t motor;
    uint16_t adc_raw;
    bool driver_good, waiting_neutral;
    uint32_t ticks, max_control_us, adc_age_ms;
} app_status_t;
void app_init(void);
void app_control_tick(void);
void app_foreground(void);
void app_stop(void);
void app_status(app_status_t *status);
#endif
