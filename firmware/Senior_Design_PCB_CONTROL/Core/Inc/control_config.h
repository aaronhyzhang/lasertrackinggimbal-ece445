#ifndef CONTROL_CONFIG_H
#define CONTROL_CONFIG_H
#include "pcb_config.h"
#include "motor_profiles.h"
#if PCB_STAGE != PCB_MOTOR_RUN || PCB_BENCH_CAPTURE_DRIVER_FAULT || PCB_BENCH_ALLOW_CPUV_HISTORY
#error Control project requires motor pin mapping and normal immediate driver shutdown
#endif
/* Initial commissioning limits, not established motor capabilities. */
#ifndef CONTROL_SOURCE_ETHERNET
#define CONTROL_SOURCE_ETHERNET 1
#endif
/* Tilt is commissioned. Pan has hard stops; keep it inhibited until its safe
 * travel/zero are defined. An explicit CLI define overrides this build default. */
#ifndef CONTROL_MOTION_ENABLED
#define CONTROL_MOTION_ENABLED (PCB_TEST_AXIS == 1)
#endif
/* First commissioning stage: encoder-based sinusoidal VOLTAGE commutation.
 * Current PI is reserved for the later FOC stage, never enabled implicitly. */
#ifndef CONTROL_CURRENT_PI_ENABLED
#define CONTROL_CURRENT_PI_ENABLED 0
#endif
/* User's fixed encoder reports MD + ML. Keep the warning visible. MD, freshness,
 * continuity and plausibility checks remain required; this is not a health pass. */
#define CONTROL_ALLOW_WEAK_MAGNET 1
#define CONTROL_ENCODER_MAX_AGE_US 4000u
#define CONTROL_COMMAND_MAX_AGE_MS 500u
#define CONTROL_IQ_LIMIT_A 0.5f
#define CONTROL_PHASE_TRIP_A 1.5f
#define CONTROL_VOLT_LIMIT 6.0f
#define CONTROL_CURRENT_KP 2.0f
#define CONTROL_CURRENT_KI 300.0f
#define CONTROL_VELOCITY_KP 0.08f
#define CONTROL_VELOCITY_KI 0.10f
#define CONTROL_CAL_VOLTS 2.0f
#define CONTROL_END_MARGIN_DEG 2.0f
#define CONTROL_LONG_PRESS_MS 2000u
#define DEMO_UDP_PORT 5005u
#if (CONTROL_SOURCE_ETHERNET != 0 && CONTROL_SOURCE_ETHERNET != 1) || \
    (CONTROL_MOTION_ENABLED != 0 && CONTROL_MOTION_ENABLED != 1) || \
    (CONTROL_CURRENT_PI_ENABLED != 0 && CONTROL_CURRENT_PI_ENABLED != 1)
#error Invalid control configuration
#endif
#endif
