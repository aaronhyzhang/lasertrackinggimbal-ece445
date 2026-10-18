#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t session; // session id
    uint32_t sequence; // number for command_handler
    bool target_detected; // true --> keep; false --> ignore
    int32_t pan_error_mdeg; // horizontal millidegrees
    int32_t tilt_error_mdeg; // vertical millidegrees
} vision_command_t;

typedef struct {
    uint32_t session; // reply session id
    uint32_t accepted_sequence; // last accepted command sequence number
    bool angles_valid;
    int32_t pan_position_mdeg;
    int32_t tilt_position_mdeg;

    bool armed;
    uint32_t fault_flags;
} telemetry_t;

typedef enum {
    PROTOCOL_OK,
    PROTOCOL_INVALID_ARGUMENT,
    PROTOCOL_INVALID_MESSAGE,
    PROTOCOL_UNSUPPORTED_VERSION,
    PROTOCOL_BUFFER_TOO_SMALL
} protocol_result_t;

// converts recieved message bytes into a vision_command_t struct
protocol_result_t protocol_parse_command(
    const char *data,
    size_t length,
    vision_command_t *command
);


// converts telemetry_t into a outgoing reply
protocol_result_t protocol_format_telemetry(
    const telemetry_t *telemetry,
    char *buffer,
    size_t capacity,
    size_t *written
);

#endif