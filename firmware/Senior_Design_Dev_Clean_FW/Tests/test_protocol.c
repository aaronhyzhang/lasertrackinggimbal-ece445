/* Computer-only tests: compile with protocol.c, never with the MCU main.c. */
#include "protocol.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expression) do { \
    checks++; \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static bool commands_equal(const vision_command_t *a, const vision_command_t *b)
{
    /* Compare fields, not padding bytes in the structure. */
    return a->session == b->session && a->sequence == b->sequence &&
           a->target_detected == b->target_detected &&
           a->pan_error_mdeg == b->pan_error_mdeg &&
           a->tilt_error_mdeg == b->tilt_error_mdeg;
}

static void expect_parse(const char *message, vision_command_t expected)
{
    vision_command_t actual = {0};
    CHECK(protocol_parse_command(message, strlen(message), &actual) == PROTOCOL_OK);
    CHECK(commands_equal(&actual, &expected));
}

static void expect_rejection(const char *data, size_t length,
                             protocol_result_t expected)
{
    const vision_command_t original = {77u, 88u, true, -123, 456};
    vision_command_t output = original;
    CHECK(protocol_parse_command(data, length, &output) == expected);
    CHECK(commands_equal(&output, &original));
}

static void test_valid_commands(void)
{
    expect_parse("V1 VISION 42 10 1 5000 -2000",
                 (vision_command_t){42u, 10u, true, 5000, -2000});
    expect_parse("V1 VISION 0 0 0 0 0",
                 (vision_command_t){0u, 0u, false, 0, 0});
    expect_parse(" \tV1\tVISION\r\n42 10 1 -5000 2000 \r\n",
                 (vision_command_t){42u, 10u, true, -5000, 2000});
    expect_parse("V1 VISION 4294967295 4294967295 1 -2147483648 2147483647",
                 (vision_command_t){UINT32_MAX, UINT32_MAX, true, INT32_MIN, INT32_MAX});
    expect_parse("V1 VISION 1 2 1 2147483647 -2147483648",
                 (vision_command_t){1u, 2u, true, INT32_MAX, INT32_MIN});
    /* Lost-target handling belongs elsewhere; parsing preserves these fields. */
    expect_parse("V1 VISION 1 2 0 5000 -2000",
                 (vision_command_t){1u, 2u, false, 5000, -2000});
}

static void test_invalid_commands(void)
{
    static const char *const invalid[] = {
        "", " \t\r\n", "V1", "V1 OTHER 1 2 1 0 0",
        "V1 VISION", "V1 VISION 1", "V1 VISION 1 2",
        "V1 VISION 1 2 1", "V1 VISION 1 2 1 0",
        "V1 VISION 1 2 1 0 0 EXTRA",
        "V1 VISION 1 2 1 0 0\nV1 VISION 1 3 1 0 0",
        "V1 VISION -1 2 1 0 0", "V1 VISION 1 -2 1 0 0",
        "V1 VISION +1 2 1 0 0", "V1 VISION 1 +2 1 0 0",
        "V1 VISION 4294967296 2 1 0 0",
        "V1 VISION 1 4294967296 1 0 0",
        "V1 VISION 1 2 2 0 0", "V1 VISION 1 2 -1 0 0",
        "V1 VISION 1 2 true 0 0",
        "V1 VISION 1 2 1 2147483648 0",
        "V1 VISION 1 2 1 -2147483649 0",
        "V1 VISION 1 2 1 0 2147483648",
        "V1 VISION 1 2 1 0 -2147483649",
        "V1 VISION 1 2 1 +5 0", "V1 VISION 1 2 1 - 0",
        "V1 VISION 1 2 1 1.5 0", "V1 VISION 1 2 1 1e3 0",
        "V1 VISION 1 2 1 0x10 0", "V1 VISION 1 2 1 NaN 0",
        "V1 VISION 1 2 1 0 5junk"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        expect_rejection(invalid[i], strlen(invalid[i]), PROTOCOL_INVALID_MESSAGE);
    }
    const char version[] = "V2 VISION 1 2 1 0 0";
    expect_rejection(version, sizeof(version) - 1, PROTOCOL_UNSUPPORTED_VERSION);
}

static void test_input_boundaries(void)
{
    const char message[] = "V1 VISION 42 10 1 5000 -2000";
    const size_t length = sizeof(message) - 1;
    /* Exact allocation has no terminating NUL. Sanitizers detect overreads. */
    char *bytes = malloc(length);
    CHECK(bytes != NULL);
    memcpy(bytes, message, length);
    vision_command_t output = {0};
    CHECK(protocol_parse_command(bytes, length, &output) == PROTOCOL_OK);
    CHECK(memcmp(bytes, message, length) == 0);
    free(bytes);

    const char with_suffix[] = "V1 VISION 42 10 1 5000 -2000 EXTRA";
    CHECK(protocol_parse_command(with_suffix, length, &output) == PROTOCOL_OK);
    /* A transmitted NUL is not part of the ASCII grammar. */
    expect_rejection(message, sizeof(message), PROTOCOL_INVALID_MESSAGE);
    const char embedded_nul[] = "V1 VISION 42 10 1 50\0 0";
    expect_rejection(embedded_nul, sizeof(embedded_nul) - 1, PROTOCOL_INVALID_MESSAGE);

    char padded[129];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, message, length);
    CHECK(protocol_parse_command(padded, 128, &output) == PROTOCOL_OK);
    expect_rejection(padded, 129, PROTOCOL_INVALID_MESSAGE);
    expect_rejection(NULL, 1, PROTOCOL_INVALID_ARGUMENT);
    CHECK(protocol_parse_command(message, length, NULL) == PROTOCOL_INVALID_ARGUMENT);
}

static void test_telemetry_values(void)
{
    char buffer[192];
    size_t written = 999;
    telemetry_t status = {42u, 10u, true, 15000, -5000, false, 0u};
    const char expected[] = "V1 STATUS 42 10 1 15000 -5000 0 0\n";
    CHECK(protocol_format_telemetry(&status, buffer, sizeof(buffer), &written) == PROTOCOL_OK);
    CHECK(strcmp(buffer, expected) == 0);
    CHECK(written == sizeof(expected) - 1);
    CHECK(buffer[written] == '\0');

    status = (telemetry_t){UINT32_MAX, UINT32_MAX, false,
                           INT32_MIN, INT32_MAX, true, UINT32_MAX};
    const char extremes[] =
        "V1 STATUS 4294967295 4294967295 0 -2147483648 2147483647 1 4294967295\n";
    CHECK(protocol_format_telemetry(&status, buffer, sizeof(buffer), &written) == PROTOCOL_OK);
    CHECK(strcmp(buffer, extremes) == 0);
    CHECK(written == sizeof(extremes) - 1);

    status = (telemetry_t){0};
    CHECK(protocol_format_telemetry(&status, buffer, sizeof(buffer), &written) == PROTOCOL_OK);
    CHECK(strcmp(buffer, "V1 STATUS 0 0 0 0 0 0 0\n") == 0);
}

static void test_telemetry_capacity(void)
{
    const telemetry_t status = {42u, 10u, true, 15000, -5000, false, 0u};
    const size_t required = sizeof("V1 STATUS 42 10 1 15000 -5000 0 0\n");
    unsigned char storage[192];
    for (size_t capacity = 0; capacity <= required; capacity++) {
        memset(storage, 0xA5, sizeof(storage));
        char *buffer = (char *)&storage[1];
        size_t written = 999;
        protocol_result_t result = protocol_format_telemetry(
            &status, buffer, capacity, &written);
        CHECK(storage[0] == 0xA5);
        /* No writes beyond the advertised capacity. */
        for (size_t i = capacity + 1; i < sizeof(storage); i++) {
            CHECK(storage[i] == 0xA5);
        }
        if (capacity < required) {
            CHECK(result == PROTOCOL_BUFFER_TOO_SMALL);
            CHECK(written == 0);
            if (capacity > 0) CHECK(buffer[0] == '\0');
        } else {
            CHECK(result == PROTOCOL_OK);
            CHECK(written == required - 1);
            CHECK(buffer[written] == '\0');
        }
    }
}

static void test_telemetry_arguments(void)
{
    const telemetry_t status = {0};
    char buffer[192];
    size_t written = 999;
    CHECK(protocol_format_telemetry(NULL, buffer, sizeof(buffer), &written) == PROTOCOL_INVALID_ARGUMENT);
    CHECK(written == 0);
    written = 999;
    CHECK(protocol_format_telemetry(&status, NULL, sizeof(buffer), &written) == PROTOCOL_INVALID_ARGUMENT);
    CHECK(written == 0);
    CHECK(protocol_format_telemetry(&status, buffer, sizeof(buffer), NULL) == PROTOCOL_INVALID_ARGUMENT);
}

int main(void)
{
    test_valid_commands();
    test_invalid_commands();
    test_input_boundaries();
    test_telemetry_values();
    test_telemetry_capacity();
    test_telemetry_arguments();
    printf("PASS: 6 protocol test groups (%u checks)\n", checks);
    return EXIT_SUCCESS;
}
