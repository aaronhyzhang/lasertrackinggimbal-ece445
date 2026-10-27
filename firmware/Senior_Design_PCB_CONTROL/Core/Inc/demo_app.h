#ifndef PCB_CONTROL_APP_H
#define PCB_CONTROL_APP_H
#include "position_control.h"
#include "open_loop.h"
#include <stddef.h>
typedef enum { SOURCE_ANALOG,SOURCE_ETHERNET } command_source_t;
typedef struct {
    position_control_t servo;
    command_source_t source;
    uint32_t ticks,max_control_us,command_age_ms;
    uint8_t test_axis;
    bool motion_enabled,current_regulation,driver_good,calibration_button_held;
    motor_fault_t board_fault;
    uint32_t foreground_age_ms,control_age_us;
} app_status_t;
extern volatile app_status_t g_app_status;
void app_init(void);
void app_control_tick(void);
void app_current_sample(void);
void app_foreground(void);
void app_stop(void);
void app_status(app_status_t *snapshot);
bool app_position(command_source_t source,uint32_t millidegrees);
bool app_heartbeat(command_source_t source);
bool app_calibrate(command_source_t source);
bool app_zero(command_source_t source);
void app_config_text(char *text,size_t length);
void app_test_text(char *text,size_t length);
size_t app_trace_read(uint32_t first,void *bytes,size_t length);
bool app_set_parameter(uint32_t key,uint32_t value);
bool app_test_prepare(const uint32_t args[6]);
void app_status_text(char *text,size_t length);
#endif
