#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { SERVO_OFF,SERVO_WAKE_CAL,SERVO_ALIGN,SERVO_SWEEP,
    SERVO_RETURN,SERVO_READY,SERVO_WAKE_RUN,SERVO_RUNNING,SERVO_FAULT } servo_state_t;
typedef enum { SERVO_OK,SERVO_DRIVER,SERVO_FEEDBACK,SERVO_ZERO_TIMEOUT,
    SERVO_CALIBRATION,SERVO_CURRENT,SERVO_CURRENT_SUM,SERVO_TRAVEL,
    SERVO_FOLLOWING,SERVO_COMMAND_TIMEOUT,SERVO_SUPPLY,SERVO_TEMPERATURE,
    SERVO_TIMING,SERVO_FOREGROUND,SERVO_INTERNAL } servo_fault_t;
typedef enum { SERVO_NO_ACTION,SERVO_STOP,SERVO_CALIBRATE,SERVO_SET_ZERO,SERVO_ARM } servo_action_t;
typedef enum { ENCODER_ACCEPTED,ENCODER_TRANSPORT,ENCODER_STALE,ENCODER_MISSING_MAGNET,
    ENCODER_WEAK_FIELD,ENCODER_SAMPLE_GAP,ENCODER_JUMP } encoder_reject_t;
typedef struct {
    float max_rpm,accel_rpm_s,position_kp,velocity_kp,velocity_ki,run_volts;
} position_config_t;
typedef struct {
    uint32_t now_us,now_ms,encoder_us,current_us,current_sequence;
    uint16_t encoder_raw;
    uint16_t current_raw[3];
    uint8_t encoder_status;
    bool encoder_transport,driver_good,hard_fault,zero_ready,current_valid,analog_valid;
    servo_fault_t hard_fault_reason;
    bool current_window_valid;
    bool temperature_valid,command_valid;
    float bus_v,temperature_c,target_deg,current_a[3]; /* Current INTO motor winding. */
    uint32_t command_ms;
    uint32_t analog_age_ms,adc_errors[3];
    uint8_t adc_fresh_mask,adc_failed_mask;
    uint32_t current_timer_count,current_timer_cr1,current_adc_clock_prescaler;
    uint8_t experiment_mode; /* 0 position, 1/3 voltage excitation, 2 velocity excitation. */
    float experiment_value;
} control_input_t;
typedef struct { bool enable,pwm; float duty[3]; } control_output_t;
typedef struct {
    position_config_t config;
    /* Latched on a new effective target; visible in Live Expressions. */
    float move_target_deg,move_distance_deg,move_accel_rpm_s;
    bool move_profile_valid;
    float settle_target_deg,settle_min_deg,settle_max_deg;
    float velocity_target_rad_s,velocity_error_rad_s,velocity_p_v,requested_v;
    bool voltage_saturated;
    servo_state_t state;
    servo_fault_t fault;
    bool commutation_calibrated,zero_set,weak_magnet,settled;
    int8_t direction;
    float electrical_offset,position_deg,velocity_rpm,requested_deg,target_deg;
    float trajectory_deg,trajectory_rpm,iq_target_a,vq_target_v,id_a,iq_a,vd_v,vq_v;
    float phase_a[3],current_sum_a,max_phase_a,duty[3];
    uint32_t encoder_age_us,current_age_us,fast_count,max_fast_us;
    uint32_t stage_ms,following_ms,settled_ms,previous_encoder_us,previous_current_us;
    uint32_t previous_sequence,sum_bad_count;
    uint16_t previous_raw;
    bool unwrap_started,velocity_started;
    float raw_turns,zero_turns,cal_start_turns,velocity_rad_s;
    float velocity_integral,id_integral,iq_integral,cal_angle;
    uint32_t cal_good_samples;
    encoder_reject_t encoder_reject,trip_encoder_reject;
    bool trip_captured;
    servo_state_t trip_state;
    control_input_t trip_input; /* First-fault evidence, never replaced by recovery. */
} position_control_t;
void position_init(position_control_t *s);
void position_stop(position_control_t *s);
void position_trip(position_control_t *s,const control_input_t *input,servo_fault_t reason);
void position_tick(position_control_t *s,const control_input_t *in,servo_action_t action);
void position_fast(position_control_t *s,const control_input_t *in,control_output_t *out);
bool position_driving(const position_control_t *s);
position_config_t position_default_config(void);
bool position_config_valid(const position_config_t *config);
float position_voltage_limit(const position_control_t *s,float bus_v);
float position_duty_span(void);
float position_effort_sign(const position_control_t *s);
#endif
