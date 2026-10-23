#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H
/* Open-loop bench limits, not motor ratings or calibrated current limits. */
#define DEMO_FW_VERSION "0.1.0-pcb-bringup"
#define DEMO_BUS_VOLTS 24.0f
#define DEMO_POLE_PAIRS 11.0f
#define DEMO_ADC_VREF 3.3f
#define DEMO_PWM_ARR 2500u /* 100 MHz / (2 * 2500) = 20 kHz. */
#define DEMO_CONTROL_HZ 1000u
#define DEMO_RUN_DEBOUNCE_MS 20u
#define DEMO_ADC_MAX_AGE_MS 20u
#define DEMO_FOREGROUND_MAX_AGE_MS 100u
#define DEMO_DRIVER_WAKE_MS 20u /* Includes foreground SPI configuration. */
#define DEMO_OPEN_MAX_RPM 100.0f
#define DEMO_OPEN_ACCEL_RPM_S 10.0f
#define DEMO_OPEN_NEUTRAL_LOW 1.3f
#define DEMO_OPEN_NEUTRAL_HIGH 1.7f
#define DEMO_OPEN_NEUTRAL_DWELL_MS 200u
#define DEMO_OPEN_COAST_MS 1000u /* Timed pause, not measured standstill. */
#define DEMO_ALIGN_VOLTS 1.0f
#define DEMO_OPEN_ALIGN_MS 2000u
#define DEMO_OPEN_ALIGN_RAMP_MS 500u
#define DEMO_OPEN_VOLTS 2.0f
#define DEMO_OPEN_VOLT_SLEW 2.0f
#endif
