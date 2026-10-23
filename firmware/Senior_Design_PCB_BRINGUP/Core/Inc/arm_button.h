#ifndef ARM_BUTTON_H
#define ARM_BUTTON_H
#include <stdbool.h>
#include <stdint.h>
#include "demo_config.h"
/* A boot-held button cannot arm. Stable release then stable press emits once.
 * Caller consumes events even while STOP is held; no delayed held-button arm. */
typedef struct {
    uint32_t since;
    bool tracking, last_pressed, released;
} arm_button_t;
static inline bool arm_button_step(arm_button_t *b,bool pressed,uint32_t now) {
    if (!b->tracking || pressed!=b->last_pressed) {
        b->tracking=true; b->last_pressed=pressed; b->since=now;
    }
    if (now-b->since<DEMO_RUN_DEBOUNCE_MS) return false;
    if (!pressed) { b->released=true; return false; }
    if (!b->released) return false;
    b->released=false; return true;
}
#endif
