#ifndef PCB_DRIVER_TRIP_H
#define PCB_DRIVER_TRIP_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    DRIVER_TRIP_NONE=0,
    DRIVER_TRIP_PWM_PERMISSION=1, /* PWM requested, ready/live-nFAULT gate failed. */
    DRIVER_TRIP_START_NFAULT_LOW=2, /* nFAULT went low at the final start check. */
    DRIVER_TRIP_NFAULT_EDGE=3, /* J4 EXTI6 falling edge during authorized PWM. */
    DRIVER_TRIP_TIM1_BREAK=4 /* J3 timer break interrupt. */
} driver_trip_source_t;
typedef struct {
    driver_trip_source_t source;
    uint32_t wake_age_ms;
    bool nfault_high,driver_ready,pwm_authorized;
} driver_trip_snapshot_t;
/* First board-layer driver trip, captured before inhibit and published after it.
 * Frozen until MCU reset. A high sampled pin can follow a real brief low pulse. */
extern volatile driver_trip_snapshot_t g_driver_trip;
#endif
