#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H
/* Bring-up defaults, not a motor rating or current limit. Change deliberately. */
#define DEMO_BUS_VOLTS 24.0f
#define DEMO_ALIGN_VOLTS 1.0f
#define DEMO_MAX_Q_VOLTS 2.0f
#define DEMO_POLE_PAIRS 11.0f
#define DEMO_MAX_SPEED 0.4f /* rad/s = 22.9 deg/s */
#define DEMO_ACCEL 0.4f /* rad/s^2 */
#define DEMO_POSITION_KP 3.0f /* (rad/s)/rad */
#define DEMO_VELOCITY_KP 1.0f /* V/(rad/s), tune on bench */
#define DEMO_VELOCITY_KI 2.0f /* V/rad, tune on bench */
#define DEMO_VELOCITY_LPF 0.05f /* seconds */
#define DEMO_ADC_VREF 3.3f /* measure VDDA and adjust for endpoint accuracy */
#define DEMO_ENCODER_MAX_AGE_MS 8u
#define DEMO_HEALTH_MAX_AGE_MS 60u
#define DEMO_ADC_MAX_AGE_MS 20u
#define DEMO_FOREGROUND_MAX_AGE_MS 100u
#define DEMO_COMMAND_MAX_AGE_MS 500u
#define DEMO_MAX_FOLLOWING_ERROR 0.5235987756f /* 30 deg between trajectory and feedback */
#define DEMO_FW_VERSION "0.1.0"
#define DEMO_PWM_ARR 2500u /* 100 MHz / (2 * 2500) = 20 kHz */
#define DEMO_CONTROL_HZ 1000u
#define DEMO_UDP_PORT 5005u
#endif
