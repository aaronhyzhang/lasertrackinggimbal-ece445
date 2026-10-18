#include "telemetry.h"

bool telemetry_collect(const command_handler_t *handler, uint32_t now_ms,
                       uint32_t timeout_ms, telemetry_t *status)
{
    if (handler == NULL || status == NULL || timeout_ms == 0u ||
        timeout_ms >= 0x80000000u) {
        return false;
    }

    telemetry_t result = {0};
    vision_command_t observation;
    observation_state_t state = command_handler_observation(
        handler, now_ms, timeout_ms, &observation);

    if (handler->has_command) {
        result.session = handler->latest.session;
        result.accepted_sequence = handler->latest.sequence;
    }
    switch (state) {
    case OBSERVATION_NONE: result.fault_flags = TELEMETRY_NO_COMMAND; break;
    case OBSERVATION_STALE: result.fault_flags = TELEMETRY_COMMAND_STALE; break;
    case OBSERVATION_TARGET_LOST: result.fault_flags = TELEMETRY_TARGET_LOST; break;
    case OBSERVATION_VALID: break;
    }
    /* Zero positions mean unavailable, not measurements: angles_valid is false. */
    *status = result;
    return true;
}
