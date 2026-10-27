#ifndef PCB_CONFIG_H
#define PCB_CONFIG_H
/* Select the bring-up stage and one axis below, then rebuild. */
#ifndef PCB_STAGE
#define PCB_STAGE PCB_MOTOR_RUN
#endif

#define PCB_IO_CHECK       0 /* Buttons/LEDs/pots; both inverters held off. */
#define PCB_PWM_CHECK      1 /* Selected MCU PWM only; both driver ENABLEs stay low. */
#define PCB_DRIVER_CHECK   2 /* Selected driver's SPI; all PWM remains off. */
#define PCB_SENSOR_CHECK   4 /* Both encoders/ADCs; S1 wakes CSAs, PWM always GPIO low. */
#define PCB_MOTOR_RUN      3 /* Selected open-loop motor, VR1, S1 ARM, S2 STOP. */
/* 0 = J3 / TIM1, 1 = J4 / TIM8. Only one axis runs at a time. */
/* VR1 commands either selected axis; VR2 is telemetry only and may be unpopulated. */
#ifndef PCB_TEST_AXIS
#define PCB_TEST_AXIS 1
#endif
/* Shared J3/J4 baseline: 0 uses the original counter/start sequence.
 * Diagnostic option 1 commits duties and restarts at count 0/up. */
#ifndef PCB_PWM_SYNC_START
#define PCB_PWM_SYNC_START 0
#endif
#if PCB_PWM_SYNC_START != 0 && PCB_PWM_SYNC_START != 1
#error Invalid PCB_PWM_SYNC_START
#endif
/* Gate-drive comparison: high-side turn-on current only. Sink stays 30 mA.
 * 15 = previous baseline, 45 = motor-disconnected diagnostic comparison. */
#ifndef PCB_HS_SOURCE_MA
#define PCB_HS_SOURCE_MA 15
#endif
#if PCB_HS_SOURCE_MA != 15 && PCB_HS_SOURCE_MA != 45
#error Invalid PCB_HS_SOURCE_MA
#endif
/* VDS protection comparison; latched shutdown remains enabled.
 * 150 = previous baseline, 240 = next motor-disconnected bench step. */
#ifndef PCB_VDS_THRESHOLD_MV
#define PCB_VDS_THRESHOLD_MV 150
#endif
#if PCB_VDS_THRESHOLD_MV != 150 && PCB_VDS_THRESHOLD_MV != 240
#error Invalid PCB_VDS_THRESHOLD_MV
#endif
/* Temporary motor-disconnected bench option. Accept only recorded CPUV;
 * driver CPUV protection and the physical nFAULT interlock remain enabled. */
#ifndef PCB_BENCH_ALLOW_CPUV_HISTORY
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#endif
/* Motor-disconnected diagnostics only: PWM off immediately; 5 ms status-read
 * budget, with control-tick deadline granularity. See docs/FAULT_READBACK.md. */
#ifndef PCB_BENCH_CAPTURE_DRIVER_FAULT
#define PCB_BENCH_CAPTURE_DRIVER_FAULT 0
#endif
#if PCB_BENCH_CAPTURE_DRIVER_FAULT != 0 && PCB_BENCH_CAPTURE_DRIVER_FAULT != 1
#error Invalid PCB_BENCH_CAPTURE_DRIVER_FAULT
#endif
#if PCB_BENCH_CAPTURE_DRIVER_FAULT && PCB_STAGE != PCB_MOTOR_RUN
#error Fault capture requires PCB_MOTOR_RUN and disconnected motors
#endif
#if PCB_STAGE < PCB_IO_CHECK || PCB_STAGE > PCB_SENSOR_CHECK
#error Invalid PCB_STAGE
#endif
#if PCB_TEST_AXIS != 0 && PCB_TEST_AXIS != 1
#error Invalid PCB_TEST_AXIS
#endif
#endif
