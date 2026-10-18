#include "telemetry.h"
#include <assert.h>
#include <stdio.h>

static void unavailable(const telemetry_t *status)
{
    assert(!status->armed && !status->angles_valid);
    assert(status->pan_position_mdeg == 0 && status->tilt_position_mdeg == 0);
}

int main(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    telemetry_t status;
    assert(telemetry_collect(&handler, 0, 500, &status));
    assert(status.session == 0 && status.accepted_sequence == 0);
    assert(status.fault_flags == TELEMETRY_NO_COMMAND);
    unavailable(&status);
    vision_command_t command = {42, 10, true, 5000, -2000};
    assert(command_handler_accept(&handler, &command, 100) == COMMAND_ACCEPTED);
    assert(telemetry_collect(&handler, 599, 500, &status));
    assert(status.session == 42 && status.accepted_sequence == 10 && status.fault_flags == 0);
    unavailable(&status);
    assert(telemetry_collect(&handler, 600, 500, &status));
    assert(status.accepted_sequence == 10 && status.fault_flags == TELEMETRY_COMMAND_STALE);
    unavailable(&status);
    command.sequence++;
    command.target_detected = false;
    assert(command_handler_accept(&handler, &command, 700) == COMMAND_ACCEPTED);
    assert(telemetry_collect(&handler, 701, 500, &status));
    assert(status.fault_flags == TELEMETRY_TARGET_LOST && status.accepted_sequence == 11);
    unavailable(&status);
    assert(telemetry_collect(&handler, 1200, 500, &status));
    assert(status.fault_flags == TELEMETRY_COMMAND_STALE);
    status.session = 123;
    assert(!telemetry_collect(NULL, 0, 500, &status));
    assert(!telemetry_collect(&handler, 0, 0, &status));
    assert(!telemetry_collect(&handler, 0, 0x80000000u, &status));
    assert(status.session == 123);
    assert(!telemetry_collect(&handler, 0, 500, NULL));
    command_handler_init(&handler);
    assert(telemetry_collect(&handler, 1300, 500, &status));
    assert(status.fault_flags == TELEMETRY_NO_COMMAND && status.session == 0);
    puts("PASS: telemetry collection (empty, fresh, stale, lost, reset, invalid arguments)");
    return 0;
}
