/* Exercise the actual BSP routing helpers against simulated BSRR registers. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint32_t BSRR; } port_t;
static port_t port_a,port_e,port_g;
#define GPIOA (&port_a)
#define GPIOE (&port_e)
#define GPIOG (&port_g)
enum {
    GPIO_PIN_0=1u<<0, GPIO_PIN_1=1u<<1, GPIO_PIN_2=1u<<2,
    GPIO_PIN_3=1u<<3, GPIO_PIN_4=1u<<4, GPIO_PIN_5=1u<<5,
    GPIO_PIN_6=1u<<6, GPIO_PIN_7=1u<<7, GPIO_PIN_8=1u<<8,
    GPIO_PIN_9=1u<<9, GPIO_PIN_10=1u<<10, GPIO_PIN_11=1u<<11,
    GPIO_PIN_12=1u<<12, GPIO_PIN_13=1u<<13, GPIO_PIN_14=1u<<14,
    GPIO_PIN_15=1u<<15
};
#include "pcb_pins.h"
static uint32_t apply(uint32_t level,uint32_t bsrr) {
    return (level|(bsrr&0xffffu))&~(bsrr>>16);
}
int main(void) {
    uint32_t both=(1u<<4)|(1u<<7);
    uint32_t selected=PCB_TEST_AXIS==0 ? 1u<<4 : 1u<<7;
    pcb_driver_enable(true);
    assert(apply(both,port_g.BSRR)==selected); /* Other ENABLE forced LOW. */
    assert((port_g.BSRR&0xffffu)==selected);
    pcb_driver_enable(false);
    assert(apply(both,port_g.BSRR)==0);
    pcb_driver_select(true);
    uint32_t cs0=apply(0,port_e.BSRR)&(1u<<5);
    uint32_t cs1=apply(0,port_a.BSRR)&(1u<<9);
    assert((cs0==0)!=(cs1==0)); /* Never both selected. */
    assert((cs0==0)==(PCB_TEST_AXIS==0));
    assert((cs1==0)==(PCB_TEST_AXIS==1));
    pcb_driver_select(false);
    assert((apply(0,port_e.BSRR)&(1u<<5))!=0);
    assert((apply(0,port_a.BSRR)&(1u<<9))!=0);
    printf("PASS: axis %d actual routing helpers select only its ENABLE/CS; disable resets both enables.\n",PCB_TEST_AXIS);
}
