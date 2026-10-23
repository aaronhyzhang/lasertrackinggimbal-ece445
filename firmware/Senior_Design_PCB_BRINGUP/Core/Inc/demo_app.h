#ifndef PCB_DEMO_APP_H
#define PCB_DEMO_APP_H
#include "open_loop.h"
typedef struct {
    motor_t motor;
    uint16_t adc_raw[2]; /* VR1/PA4 then VR2/PA0. Only VR1 commands motion. */
    float command_volts; /* Normalized 0..3 command, not a voltmeter reading. */
    uint32_t ticks,max_control_us,adc_age_ms;
    uint8_t stage,test_axis; /* 0=J3, 1=J4; axis 1 is driver/IO checks only. */
    bool arm_pressed,stop_pressed,driver_good,waiting_neutral,adc_valid;
} app_status_t;
/* Watch in CubeIDE Live Expressions. Read only; never use debug writes to arm. */
extern volatile app_status_t g_app_status;
void app_init(void);
void app_control_tick(void);
void app_foreground(void);
void app_stop(void);
void app_status(app_status_t *snapshot);
#endif
