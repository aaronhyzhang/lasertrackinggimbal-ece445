#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "command_handler.h"

/* Data-only status conditions carried in the existing fault_flags field.
   These are NOT motor/encoder fault assignments. */
#define TELEMETRY_NO_COMMAND   (1u << 0)
#define TELEMETRY_COMMAND_STALE (1u << 1)
#define TELEMETRY_TARGET_LOST  (1u << 2)

/* Snapshot accepted session/sequence and observation health.
   No encoder or motor exists in this profile: angles_valid/armed remain false.
   Required pointers must be non-NULL; timeout follows command_handler's contract. */
bool telemetry_collect(const command_handler_t *handler, uint32_t now_ms,
                       uint32_t timeout_ms, telemetry_t *status);

#endif
