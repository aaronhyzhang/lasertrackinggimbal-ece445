#include "command_handler.h"

/* Compare sequence numbers while allowing uint32_t wraparound.
   Exactly half the counter range is treated as ambiguous/rejected. */
static bool sequence_is_newer(uint32_t incoming, uint32_t previous)
{
    uint32_t difference = incoming - previous;
    return difference != 0u && difference < 0x80000000u;
}

void command_handler_init(command_handler_t *handler)
{
    if (handler == NULL) {
        return;
    }
    *handler = (command_handler_t){0};
}

command_result_t command_handler_accept(
    command_handler_t *handler,
    const vision_command_t *command,
    uint32_t now_ms)
{
    if (handler == NULL || command == NULL) {
        return COMMAND_INVALID_ARGUMENT;
    }

    if (handler->has_command) {
        /* A different client session cannot take ownership automatically. */
        if (command->session != handler->latest.session) {
            return COMMAND_SESSION_MISMATCH;
        }

        /* Reject duplicate and older commands without refreshing time. */
        if (!sequence_is_newer(
                command->sequence,
                handler->latest.sequence)) {
            return COMMAND_OLD_SEQUENCE;
        }
    }

    /* First command or a newer command from the current session. */
    handler->latest = *command;
    handler->received_ms = now_ms;
    handler->has_command = true;

    return COMMAND_ACCEPTED;
}

observation_state_t command_handler_observation(
    const command_handler_t *handler,
    uint32_t now_ms,
    uint32_t timeout_ms,
    vision_command_t *observation)
{
    /* Invalid arguments provide no usable observation. */
    if (handler == NULL || observation == NULL ||
        timeout_ms == 0u || timeout_ms >= 0x80000000u) {
        return OBSERVATION_NONE;
    }

    if (!handler->has_command) {
        return OBSERVATION_NONE;
    }

    /* Unsigned subtraction handles a millisecond-counter rollover. */
    uint32_t age_ms = now_ms - handler->received_ms;

    /* Expire at the timeout boundary, not one tick afterward. */
    if (age_ms >= timeout_ms) {
        return OBSERVATION_STALE;
    }

    if (!handler->latest.target_detected) {
        return OBSERVATION_TARGET_LOST;
    }

    /* Only publish an observation when it is fresh and detected. */
    *observation = handler->latest;

    return OBSERVATION_VALID;
}