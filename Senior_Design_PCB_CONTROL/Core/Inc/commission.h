#ifndef COMMISSION_H
#define COMMISSION_H
#include "position_control.h"
#include <stddef.h>
#define COMMISSION_TRACE_CAPACITY 2048u
typedef struct {
    uint32_t sequence,us,encoder_us,config_revision,trial;
    float position,velocity,target,trajectory,velocity_target,velocity_error;
    float p_term,i_term,requested_v,applied_v,bus_v,current[3],temperature;
    uint32_t flags;
    uint16_t state,fault;
} commission_sample_t;
_Static_assert(sizeof(commission_sample_t)==88,"Trace wire schema");
typedef struct {
    uint32_t trial,mode,amplitude,period_ms,duration_ms,centre_mdeg,radius_mdeg;
    uint32_t started_ms,next_sequence,revision;
    bool prepared,running,complete,aborted,authorized;
} commission_state_t;
extern commission_state_t g_commission;
void commission_init(void);
bool commission_prepare(uint32_t mode,uint32_t amplitude,uint32_t period,uint32_t duration,
                        uint32_t centre,uint32_t radius);
void commission_abort(void);
/* Returns true on normal completion; caller inhibits without retaining PWM. */
bool commission_input(control_input_t *in,const position_control_t *servo);
void commission_capture(const control_input_t *in,const position_control_t *servo);
size_t commission_read(uint32_t first,void *bytes,size_t capacity);
#endif
