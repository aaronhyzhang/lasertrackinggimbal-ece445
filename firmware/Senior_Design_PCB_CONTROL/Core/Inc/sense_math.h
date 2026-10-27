#ifndef SENSE_MATH_H
#define SENSE_MATH_H
#include <stdbool.h>
#include <stdint.h>
#define SENSE_AXES 2u
#define SENSE_PHASES 3u
#define SENSE_ZERO_SAMPLES 256u
#define SENSE_ADC_MAX_AGE_MS 20u
#define SENSE_ENCODER_MAX_AGE_US 10000u
#define SENSE_ENCODER_GAP_US 50000u /* Unwrap is ambiguous after a long gap. */
typedef enum {
    SENSE_ZERO_NOT_STARTED,SENSE_ZERO_ASLEEP,SENSE_ZERO_WAIT_DRIVER,
    SENSE_ZERO_WRONG_GAIN,SENSE_ZERO_WAIT_ADC,SENSE_ZERO_WAIT_REFERENCE,
    SENSE_ZERO_PWM_ACTIVE,SENSE_ZERO_INPUT_RANGE,SENSE_ZERO_UNSTABLE,
    SENSE_ZERO_COLLECTING,SENSE_ZERO_READY
} sense_zero_state_t;
typedef struct {
    uint16_t raw_angle,configuration,magnitude;
    uint8_t status,agc;
    float angle_deg,position_deg,velocity_rpm;
    uint32_t sample_count,error_count,timeout_count,last_error;
    uint32_t sample_us,age_us,health_age_ms,transaction_us,max_transaction_us;
    bool valid,magnet_ok,position_valid,velocity_valid,configuration_valid;
} sense_encoder_t;
typedef struct {
    bool awake,configured,pwm_active;
    uint32_t epoch;
    uint16_t csa_register;
    uint16_t reg[7]; /* Last SPI snapshot; read_mask distinguishes unread zeros. */
    uint8_t read_mask,failed_address;
    float gain_v_per_v;
} sense_driver_t;
typedef struct {
    sense_encoder_t encoder;
    sense_driver_t driver;
    uint16_t current_raw[3],phase_raw[3],temperature_raw;
    float current_v[3],current_zero_v[3],current_a[3],phase_v[3];
    float temperature_v,thermistor_ohm,temperature_c;
    uint32_t current_age_ms,phase_age_ms,current_zero_samples;
    bool current_zero_valid,current_valid,current_window_valid,phase_valid,temperature_valid;
    uint8_t current_saturated_mask,phase_saturated_mask;
    sense_zero_state_t current_zero_state,current_zero_last_reject;
    uint8_t current_zero_reject_mask; /* Bits 0/1/2 = A/B/C; last rejected sample. */
    float current_zero_reject_v[3];
} sense_axis_t;
typedef struct {
    sense_axis_t axis[2];
    uint16_t pot_raw[2],bus_raw,vrefint_raw;
    float pot_v[2],bus_pin_v,bus_v,adc_reference_v;
    bool reference_valid,pot_valid,bus_valid,sensor_check;
    uint32_t adc_age_ms[3],adc_count[3],adc_errors[3],snapshot_count,snapshot_ms;
    uint32_t max_acquisition_us;
} feedback_t;
typedef struct {
    uint16_t previous_raw;
    int64_t counts;
    uint32_t previous_us;
    bool initialized,continuity_lost;
    float velocity_rpm;
} sense_unwrap_t;
typedef struct {
    double sum[3];
    float minimum[3],maximum[3],zero[3];
    uint32_t epoch,count;
    bool valid;
    sense_zero_state_t state,last_reject;
    uint8_t reject_mask;
    float reject_v[3]; /* Last rejection survives sleep/fault for diagnostics. */
} sense_zero_t;
float sense_pin_volts(uint16_t raw,float reference);
float sense_reference_volts(uint16_t raw,uint16_t factory_cal);
float sense_current_amps(float output_v,float zero_v);
bool sense_temperature(uint16_t raw,float *ohms,float *degrees_c);
void sense_encoder_update(sense_unwrap_t *state,sense_encoder_t *out,
                          uint16_t raw,uint8_t status,uint32_t now_us);
void sense_encoder_age(sense_encoder_t *out,uint32_t now_us);
void sense_zero_update(sense_zero_t *state,const float voltage[3],
                       const sense_driver_t *driver,bool adc_valid,float reference);
#endif
