#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "protocol.h"

typedef enum {
    COMMAND_ACCEPTED,
    COMMAND_INVALID_ARGUMENT,
    COMMAND_SESSION_MISMATCH,
    COMMAND_OLD_SEQUENCE
} command_result_t;

typedef enum {
    OBSERVATION_NONE,
    OBSERVATION_VALID,
    OBSERVATION_TARGET_LOST,
    OBSERVATION_STALE
} observation_state_t;

typedef struct {
    bool has_command;
    vision_command_t latest;
    uint32_t received_ms;
} command_handler_t;

/* Initialize, or explicitly clear the session and stored observation.
   Application must only reset ownership while movement is disabled. */
void command_handler_init(command_handler_t *handler);

/* Accept an already parsed command.
   Rejection must leave the handler unchanged. */
command_result_t command_handler_accept(
    command_handler_t *handler,
    const vision_command_t *command,
    uint32_t now_ms
);

/* Check whether the stored observation is still usable.
   Writes observation only when returning OBSERVATION_VALID.
   handler and observation must be non-NULL.
   timeout_ms must be > 0 and < 0x80000000. */
observation_state_t command_handler_observation(
    const command_handler_t *handler,
    uint32_t now_ms,
    uint32_t timeout_ms,
    vision_command_t *observation
);

#endif