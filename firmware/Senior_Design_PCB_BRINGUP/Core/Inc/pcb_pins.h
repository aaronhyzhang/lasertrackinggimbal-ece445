#ifndef PCB_PINS_H
#define PCB_PINS_H
/* Reviewed nine-page schematic, physical pages 3,5,6,7. J3 / axis 0 only. */
#define PCB_ENABLES       (GPIO_PIN_4 | GPIO_PIN_7) /* PG4/PG7, active high */
#define PCB_CALS          (GPIO_PIN_5 | GPIO_PIN_6) /* PG5/PG6 */
#define PCB_PWM0_E        (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13)
#define PCB_PWM0_B        GPIO_PIN_0 /* TIM1_CH2N; NOT Nucleo PE10 */
#define PCB_PWM1_A        GPIO_PIN_5
#define PCB_PWM1_B        (GPIO_PIN_14 | GPIO_PIN_15)
#define PCB_PWM1_C        (GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8)
#define PCB_CS0           GPIO_PIN_5 /* PE5, active low */
#define PCB_CS1           GPIO_PIN_9 /* PA9, active low */
#define PCB_NFAULT        GPIO_PIN_6 /* PE6 / TIM1_BKIN2 */
#define PCB_ARM           GPIO_PIN_8 /* PD8 / S1, pressed low */
#define PCB_STOP          GPIO_PIN_9 /* PD9 / S2, pressed low */
#define PCB_BLUE          GPIO_PIN_12 /* PD12 / LED1, active high */
#define PCB_GREEN         GPIO_PIN_11 /* PD11 / LED2, active high */
#define PCB_RED           GPIO_PIN_10 /* PD10 / LED3, active high */
#define PCB_VR1_PIN       GPIO_PIN_4  /* PA4 / POT1 / ADC1_INP18 / TP12 */
#define PCB_VR2_PIN       GPIO_PIN_0  /* PA0 / POT0 / ADC1_INP16 / TP13 */
#endif
