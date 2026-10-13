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
#ifndef DEMO_DEFAULT_OPEN_LOOP
#define DEMO_DEFAULT_OPEN_LOOP 1 /* CHANGE THIS LINE: 1=open-loop speed, 0=closed-loop position. */
#endif
#if DEMO_DEFAULT_OPEN_LOOP != 0 && DEMO_DEFAULT_OPEN_LOOP != 1
#error "DEMO_DEFAULT_OPEN_LOOP must be 0 or 1"
#endif
#define DEMO_OPEN_MAX_RPM 100.0f /* 3 V full scale; higher-speed bench validation pending. */
#define DEMO_OPEN_ACCEL_RPM_S 10.0f
#define DEMO_OPEN_VOLTS 2.0f /* Fixed rotating-vector magnitude, not a current limit. */
#define DEMO_OPEN_VOLT_SLEW 2.0f /* V/s, from alignment magnitude to running magnitude. */
#define DEMO_OPEN_ZERO_VOLTS 0.10f /* Pot bottoms near 40 mV; <=100 mV is zero, above uses V/3 scaling. */
#define DEMO_OPEN_ARM_MAX_VOLTS 0.10f /* Require the zero-speed deadband before ARM. */
#define DEMO_OPEN_ALIGN_MS 2000u
#define DEMO_OPEN_ALIGN_RAMP_MS 500u
#ifndef DEMO_PWM_SCOPE_TEST
#define DEMO_PWM_SCOPE_TEST 0 /* 1=automatic MCU PWM scope test, driver ENABLE always LOW; 0=motor firmware. */
#endif
#if DEMO_PWM_SCOPE_TEST != 0 && DEMO_PWM_SCOPE_TEST != 1
#error "DEMO_PWM_SCOPE_TEST must be 0 or 1"
#endif
#ifndef DEMO_OPEN_BOARD_CONTROL
#define DEMO_OPEN_BOARD_CONTROL 1 /* Open loop: PG2 physical RUN, no console/encoder/network required. */
#endif
#if DEMO_OPEN_BOARD_CONTROL != 0 && DEMO_OPEN_BOARD_CONTROL != 1
#error "DEMO_OPEN_BOARD_CONTROL must be 0 or 1"
#endif
#define DEMO_BOARD_OPEN_ACTIVE (DEMO_OPEN_BOARD_CONTROL && DEMO_DEFAULT_OPEN_LOOP && !DEMO_PWM_SCOPE_TEST)
#define DEMO_RUN_DEBOUNCE_MS 20u
#define DEMO_FW_VERSION "0.4.2"
#define DEMO_PWM_ARR 2500u /* 100 MHz / (2 * 2500) = 20 kHz */
#define DEMO_CONTROL_HZ 1000u
#define DEMO_UDP_PORT 5005u
#endif
