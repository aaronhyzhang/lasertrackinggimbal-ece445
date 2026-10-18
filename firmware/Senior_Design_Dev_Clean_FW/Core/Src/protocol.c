#include "protocol.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_MAX_LENGTH 128u //max accepted command length in bytes

typedef struct {
    const char *data;
    size_t length;
    size_t position;
} reader_t;


// different ways of accepting spaces, tabs, lines, endings between fields
static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// returns the next token without modifying input
static bool next_token(
    reader_t *reader,
    const char **token,
    size_t *length)
{
    while (reader->position < reader->length &&
           is_space(reader->data[reader->position])) {
        reader->position++;
    }

    size_t start = reader->position;

    while (reader->position < reader->length &&
           !is_space(reader->data[reader->position])) {
        reader->position++;
    }
    *token = reader->data + start;
    *length = reader->position - start;
    return *length != 0;
}

static bool token_equals(
    const char *token,
    size_t length,
    const char *expected)
{
    return length == strlen(expected) &&
           memcmp(token, expected, length) == 0;
}

/* Parse decimal digits while rejecting overflow. */
static bool parse_unsigned(
    const char *token,
    size_t length,
    uint32_t maximum,
    uint32_t *result)
{
    if (length == 0) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < length; i++) {
        if (token[i] < '0' || token[i] > '9') {
            return false;
        }
        uint32_t digit = (uint32_t)(token[i] - '0');
        if (value > maximum / 10u ||
            (value == maximum / 10u && digit > maximum % 10u)) {
            return false;
        }
        value = value * 10u + digit;
    }
    *result = value;
    return true;
}

static bool read_unsigned(
    reader_t *reader,
    uint32_t maximum,
    uint32_t *result)
{
    const char *token;
    size_t length;

    return next_token(reader, &token, &length) &&
           parse_unsigned(token, length, maximum, result);
}

static bool read_signed(reader_t *reader, int32_t *result)
{
    const char *token;
    size_t length;

    if (!next_token(reader, &token, &length)) {
        return false;
    }

    bool negative = token[0] == '-';

    if (negative) {
        token++;
        length--;
    }

    uint32_t magnitude;
    uint32_t maximum = negative
        ? (uint32_t)INT32_MAX + 1u
        : (uint32_t)INT32_MAX;

    if (!parse_unsigned(token, length, maximum, &magnitude)) {
        return false;
    }

    /* Use a wider intermediate to handle INT32_MIN safely. */
    *result = negative
        ? (int32_t)(-(int64_t)magnitude)
        : (int32_t)magnitude;

    return true;
}

protocol_result_t protocol_parse_command(
    const char *data,
    size_t length,
    vision_command_t *command)
{
    if (data == NULL || command == NULL) {
        return PROTOCOL_INVALID_ARGUMENT;
    }

    if (length == 0 || length > COMMAND_MAX_LENGTH) {
        return PROTOCOL_INVALID_MESSAGE;
    }

    reader_t reader = {
        .data = data,
        .length = length,
        .position = 0
    };

    const char *token;
    size_t token_length;

    if (!next_token(&reader, &token, &token_length)) {
        return PROTOCOL_INVALID_MESSAGE;
    }

    if (!token_equals(token, token_length, "V1")) {
        return PROTOCOL_UNSUPPORTED_VERSION;
    }

    if (!next_token(&reader, &token, &token_length) ||
        !token_equals(token, token_length, "VISION")) {
        return PROTOCOL_INVALID_MESSAGE;
    }

    /* Parse into a temporary object; preserve caller data on failure. */
    vision_command_t parsed = {0};
    uint32_t detected;

    if (!read_unsigned(&reader, UINT32_MAX, &parsed.session) ||
        !read_unsigned(&reader, UINT32_MAX, &parsed.sequence) ||
        !read_unsigned(&reader, 1u, &detected) ||
        !read_signed(&reader, &parsed.pan_error_mdeg) ||
        !read_signed(&reader, &parsed.tilt_error_mdeg)) {
        return PROTOCOL_INVALID_MESSAGE;
    }

    /* Reject any unexpected fields after the final angle. */
    if (next_token(&reader, &token, &token_length)) {
        return PROTOCOL_INVALID_MESSAGE;
    }

    parsed.target_detected = detected != 0u;
    *command = parsed;

    return PROTOCOL_OK;
}

protocol_result_t protocol_format_telemetry(
    const telemetry_t *telemetry,
    char *buffer,
    size_t capacity,
    size_t *written)
{
    if (written == NULL) {
        return PROTOCOL_INVALID_ARGUMENT;
    }

    *written = 0;

    if (telemetry == NULL || buffer == NULL) {
        return PROTOCOL_INVALID_ARGUMENT;
    }

    if (capacity == 0) {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    int count = snprintf(
        buffer,
        capacity,
        "V1 STATUS %" PRIu32 " %" PRIu32 " %u "
        "%" PRId32 " %" PRId32 " %u %" PRIu32 "\n",
        telemetry->session,
        telemetry->accepted_sequence,
        (unsigned)telemetry->angles_valid,
        telemetry->pan_position_mdeg,
        telemetry->tilt_position_mdeg,
        (unsigned)telemetry->armed,
        telemetry->fault_flags
    );

    if (count < 0) {
        buffer[0] = '\0';
        return PROTOCOL_INVALID_MESSAGE;
    }

    if ((size_t)count >= capacity) {
        buffer[0] = '\0';
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    *written = (size_t)count;
    return PROTOCOL_OK;
}