#include "pwm_scope.h"
#include <assert.h>
#include <stdio.h>
static uint16_t raw(float volts) { return (uint16_t)(volts / DEMO_ADC_VREF * 65535.0f); }
int main(void) {
    assert(pwm_scope_compare(raw(0.04f),true,0,true)==0);
    assert(pwm_scope_compare(raw(0.10f),true,0,true)==0);
    assert(pwm_scope_compare(raw(0.11f),true,0,true)==125);
    uint32_t half=pwm_scope_compare(raw(1.5f),true,0,true);
    assert(half>=1249 && half<=1250);
    assert(pwm_scope_compare(raw(3.0f),true,0,true)==2375);
    assert(pwm_scope_compare(raw(3.1f),true,0,true)==2375);
    assert(pwm_scope_compare(raw(3.2f),true,0,true)==0);
    assert(pwm_scope_compare(raw(1.5f),false,0,true)==0);
    assert(pwm_scope_compare(raw(1.5f),true,DEMO_ADC_MAX_AGE_MS+1,true)==0);
    assert(pwm_scope_compare(raw(1.5f),true,0,false)==0);
    assert(pwm_scope_compare(raw(1.5f),true,DEMO_ADC_MAX_AGE_MS,true)>0);
    puts("PASS: MCU scope PWM duty, 40/100 mV zero input, ADC validity/age/range and permit input.");
    return 0;
}
