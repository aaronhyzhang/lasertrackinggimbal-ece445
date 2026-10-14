#ifndef DEMO_RUN_SWITCH_H
#define DEMO_RUN_SWITCH_H
#include "open_loop.h"
#include "demo_config.h"
/* LOW stops immediately; arming needs stable LOW then stable HIGH.
 * A HIGH edge emits one request; the caller may wait for valid center input. */
typedef struct {
    uint32_t low_since, high_since;
    bool low_tracking, high_tracking, seen_low;
} run_switch_t;
static inline motor_action_t run_switch_step(run_switch_t *s, bool high, uint32_t now) {
    if (!high) {
        if (!s->low_tracking) { s->low_tracking=true; s->low_since=now; }
        s->high_tracking=false;
        if (now-s->low_since>=DEMO_RUN_DEBOUNCE_MS) s->seen_low=true;
        return ACTION_DISABLE;
    }
    s->low_tracking=false;
    if (!s->high_tracking) { s->high_tracking=true; s->high_since=now; }
    if (s->seen_low && now-s->high_since>=DEMO_RUN_DEBOUNCE_MS) {
        s->seen_low=false; return ACTION_ARM;
    }
    return ACTION_NONE;
}
#endif
