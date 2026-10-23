#ifndef PCB_CONFIG_H
#define PCB_CONFIG_H
/* Change this ONE line between the documented bring-up steps, then rebuild. */
#ifndef PCB_STAGE
#define PCB_STAGE PCB_IO_CHECK
#endif

#define PCB_IO_CHECK       0 /* Buttons/LEDs/pots; both inverters held off. */
#define PCB_PWM_CHECK      1 /* J3 MCU PWM only; both driver ENABLEs stay low. */
#define PCB_DRIVER_CHECK   2 /* J3 SPI wake/readback; all PWM remains off. */
#define PCB_MOTOR_RUN      3 /* J3 open-loop motor, VR1, S1 ARM, S2 STOP. */
#if PCB_STAGE < PCB_IO_CHECK || PCB_STAGE > PCB_MOTOR_RUN
#error Invalid PCB_STAGE
#endif
#endif
