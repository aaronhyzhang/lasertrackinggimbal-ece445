#ifndef DEMO_APP_H
#define DEMO_APP_H
#include "motor_control.h"
#include <stddef.h>
typedef struct {
    motor_t motor;
    command_source_t source;
    uint16_t adc_raw, encoder_raw;
    bool magnet_good, driver_good;
    uint32_t ticks, max_control_us, command_ms;
    uint32_t encoder_age_ms, health_age_ms, adc_age_ms;
} app_status_t;
void app_init(void);
void app_control_tick(void);
void app_foreground(void);
bool app_request(motor_action_t action);
bool app_mode(motor_mode_t mode);
void app_stop(void);
bool app_source(command_source_t source);
bool app_position(command_source_t source, uint32_t millidegrees);
bool app_heartbeat(command_source_t source);
void app_status(app_status_t *status);
size_t app_status_text(char *text,size_t capacity);
#endif
