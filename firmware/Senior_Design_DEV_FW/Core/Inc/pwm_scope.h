#ifndef DEMO_PWM_SCOPE_H
#define DEMO_PWM_SCOPE_H
#include "demo_config.h"
#include <stdbool.h>
#include <stdint.h>
/* MCU-only waveform check: 0 means inhibit ALL outputs, never authorize ENABLE.
 * All three legs use this same compare value; this is not motor commutation. */
static inline uint32_t pwm_scope_compare(uint16_t raw, bool valid,
                                         uint32_t age_ms, bool permit) {
    if (!valid || age_ms > DEMO_ADC_MAX_AGE_MS || !permit) return 0;
    float volts = (float)raw * (DEMO_ADC_VREF / 65535.0f);
    if (volts <= DEMO_OPEN_ZERO_VOLTS || volts > 3.15f) return 0;
    float duty = volts / 3.0f;
    if (duty < 0.05f) duty = 0.05f;
    if (duty > 0.95f) duty = 0.95f;
    return (uint32_t)(duty * DEMO_PWM_ARR);
}
#endif
