#ifndef PCB_CONFIG_H
#define PCB_CONFIG_H
/* Change this ONE line between the documented bring-up steps, then rebuild. */
#ifndef PCB_STAGE
#define PCB_STAGE PCB_IO_CHECK
#endif

#define PCB_IO_CHECK       0 /* Buttons/LEDs/pots; both inverters held off. */
#define PCB_PWM_CHECK      1 /* J3 MCU PWM only; both driver ENABLEs stay low. */
#define PCB_DRIVER_CHECK   2 /* Selected driver's SPI; all PWM remains off. */
#define PCB_MOTOR_RUN      3 /* J3 open-loop motor, VR1, S1 ARM, S2 STOP. */
/* 0 = J3, 1 = J4. J4 currently supports IO/driver checks only, not PWM. */
#ifndef PCB_TEST_AXIS
#define PCB_TEST_AXIS 0
#endif
/* Temporary motor-disconnected bench option. Accept only recorded CPUV;
 * driver CPUV protection and the physical nFAULT interlock remain enabled. */
#ifndef PCB_BENCH_ALLOW_CPUV_HISTORY
#define PCB_BENCH_ALLOW_CPUV_HISTORY 0
#endif
#if PCB_STAGE < PCB_IO_CHECK || PCB_STAGE > PCB_MOTOR_RUN
#error Invalid PCB_STAGE
#endif
#if PCB_TEST_AXIS != 0 && PCB_TEST_AXIS != 1
#error Invalid PCB_TEST_AXIS
#endif
#if PCB_TEST_AXIS == 1 && PCB_STAGE != PCB_IO_CHECK && PCB_STAGE != PCB_DRIVER_CHECK
#error Axis 1 supports only PCB_IO_CHECK and PCB_DRIVER_CHECK; TIM8 PWM is not implemented
#endif
#endif
