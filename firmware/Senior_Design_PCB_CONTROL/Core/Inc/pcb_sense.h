#ifndef PCB_SENSE_H
#define PCB_SENSE_H
#include "stm32h7xx_hal.h"
#include "sense_math.h"
#include "position_control.h"
extern ADC_HandleTypeDef hadc1,hadc2,hadc3;
extern I2C_HandleTypeDef hi2c1,hi2c2;
/* Read-only debugger snapshot, refreshed at 20 Hz. No control inputs. */
extern volatile feedback_t g_feedback;
/* Compact Live Expressions view: same converted diagnostic values as g_feedback.
 * Check status before trusting a reading; finite values may be stale/invalid.
 * Phase voltages are filtered switch-node voltages relative to board ground. */
typedef struct {
    float current_a,voltage_v;
} telemetry_phase_t;
typedef struct {
    bool encoder_angle_valid,encoder_position_valid,encoder_velocity_valid;
    bool current_calibrated,current_valid,current_window_valid;
    bool phase_voltage_valid,temperature_valid;
    sense_zero_state_t current_calibration_state,current_calibration_last_reject;
    uint32_t current_calibration_samples;
} telemetry_output_status_t;
typedef struct {
    float encoder_angle_deg,encoder_position_deg,encoder_velocity_rpm;
    telemetry_phase_t phase_a,phase_b,phase_c;
    float temperature_c;
    telemetry_output_status_t status;
} telemetry_output_t;
typedef struct {
    bool bus_voltage_valid,pot_voltage_valid;
} telemetry_status_t;
typedef struct {
    telemetry_output_t output_j3,output_j4; /* Axis 0 and axis 1 respectively. */
    float bus_voltage_v,pot1_voltage_v,pot2_voltage_v;
    telemetry_status_t status;
} telemetry_t;
extern volatile telemetry_t g_telemetry; /* Read-only; same 20 Hz publication. */
void pcb_sense_init(void);
void pcb_sense_start(void); /* First ADC enable runs before the control IRQ starts. */
void pcb_sense_tick(void);
void pcb_sense_service(void);
void pcb_sense_pots(uint16_t raw[2],bool *valid,uint32_t *sample_ms);
bool pcb_sense_current_zero_ready(unsigned axis,uint32_t epoch);
void board_feedback_driver(unsigned axis,sense_driver_t *driver);
/* Fresh acquisition state, independent of the 20 Hz debugger publication. */
void pcb_control_read(control_input_t *input);
void app_current_sample(void);
void pcb_sense_adc_resume(ADC_HandleTypeDef *adc);
#endif
