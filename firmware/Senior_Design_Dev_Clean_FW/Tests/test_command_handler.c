/* Host tests: no HAL, Ethernet or motor hardware required. */
#include "command_handler.h"

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
    return a->session == b->session && a->sequence == b->sequence &&
           a->target_detected == b->target_detected &&
           a->pan_error_mdeg == b->pan_error_mdeg &&
           a->tilt_error_mdeg == b->tilt_error_mdeg;
}

static bool handlers_equal(const command_handler_t *a, const command_handler_t *b)
{
    return a->has_command == b->has_command &&
           a->received_ms == b->received_ms &&
           commands_equal(&a->latest, &b->latest);
}

static void expect_unusable(const command_handler_t *handler, uint32_t now,
                            uint32_t timeout, observation_state_t expected)
{
    const vision_command_t sentinel = {99u, 88u, true, -123, 456};
    vision_command_t output = sentinel;
    CHECK(command_handler_observation(handler, now, timeout, &output) == expected);
    CHECK(commands_equal(&output, &sentinel));
}

static void test_initialization(void)
{
    command_handler_t handler = {true, {42u, 10u, true, 5000, -2000}, 123u};
    command_handler_init(&handler);
    CHECK(!handler.has_command);
    CHECK(handler.received_ms == 0u);
    const vision_command_t empty = {0};
    CHECK(commands_equal(&handler.latest, &empty));
    expect_unusable(&handler, 100u, 500u, OBSERVATION_NONE);
}

static void test_acceptance(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    vision_command_t command = {42u, 10u, true, 5000, -2000};
    const vision_command_t original = command;
    CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
    CHECK(commands_equal(&command, &original));
    CHECK(handler.has_command);
    CHECK(handler.received_ms == 100u);
    CHECK(commands_equal(&handler.latest, &command));
    vision_command_t output = {0};
    CHECK(command_handler_observation(&handler, 100u, 500u, &output) == OBSERVATION_VALID);
    CHECK(commands_equal(&output, &command));
    /* Missing a sequence number is allowed; only the latest observation matters. */
    command.sequence = 12u;
    command.pan_error_mdeg = -1000;
    CHECK(command_handler_accept(&handler, &command, 200u) == COMMAND_ACCEPTED);
    CHECK(handler.received_ms == 200u);
    CHECK(commands_equal(&handler.latest, &command));
}

static void test_rejection_preserves_state(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    const vision_command_t initial = {42u, 10u, true, 5000, -2000};
    CHECK(command_handler_accept(&handler, &initial, 100u) == COMMAND_ACCEPTED);
    const command_handler_t saved = handler;
    vision_command_t incoming = {42u, 10u, false, -9000, 8000};
    CHECK(command_handler_accept(&handler, &incoming, 300u) == COMMAND_OLD_SEQUENCE);
    CHECK(handlers_equal(&handler, &saved));
    incoming.sequence = 9u;
    CHECK(command_handler_accept(&handler, &incoming, 400u) == COMMAND_OLD_SEQUENCE);
    CHECK(handlers_equal(&handler, &saved));
    incoming.session = 43u;
    incoming.sequence = 11u;
    CHECK(command_handler_accept(&handler, &incoming, 599u) == COMMAND_SESSION_MISMATCH);
    CHECK(handlers_equal(&handler, &saved));
    /* Rejected traffic must not extend the original 500 ms lifetime. */
    expect_unusable(&handler, 600u, 500u, OBSERVATION_STALE);
}

static void test_sequence_boundaries(void)
{
    static const struct {
        uint32_t previous;
        uint32_t incoming;
        command_result_t expected;
    } cases[] = {
        {0u, 0u, COMMAND_OLD_SEQUENCE},
        {0u, 1u, COMMAND_ACCEPTED},
        {0u, 0x7fffffffu, COMMAND_ACCEPTED},
        {0u, 0x80000000u, COMMAND_OLD_SEQUENCE},
        {0u, UINT32_MAX, COMMAND_OLD_SEQUENCE},
        {UINT32_MAX, 0u, COMMAND_ACCEPTED},
        {UINT32_MAX - 1u, 1u, COMMAND_ACCEPTED},
        {1u, UINT32_MAX, COMMAND_OLD_SEQUENCE},
        {10u, 0x8000000au, COMMAND_OLD_SEQUENCE}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        command_handler_t handler;
        command_handler_init(&handler);
        vision_command_t command = {42u, cases[i].previous, true, 0, 0};
        CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
        const command_handler_t saved = handler;
        command.sequence = cases[i].incoming;
        CHECK(command_handler_accept(&handler, &command, 200u) == cases[i].expected);
        if (cases[i].expected == COMMAND_ACCEPTED) {
            CHECK(commands_equal(&handler.latest, &command));
            CHECK(handler.received_ms == 200u);
        } else {
            CHECK(handlers_equal(&handler, &saved));
        }
    }
}

static void test_timeout_and_refresh(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    vision_command_t command = {42u, 10u, true, 5000, -2000};
    CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
    vision_command_t output = {0};
    CHECK(command_handler_observation(&handler, 599u, 500u, &output) == OBSERVATION_VALID);
    expect_unusable(&handler, 600u, 500u, OBSERVATION_STALE);
    expect_unusable(&handler, 601u, 500u, OBSERVATION_STALE);
    /* A newer same-session observation can become fresh again; this is not arming. */
    command.sequence++;
    CHECK(command_handler_accept(&handler, &command, 700u) == COMMAND_ACCEPTED);
    CHECK(command_handler_observation(&handler, 1199u, 500u, &output) == OBSERVATION_VALID);
    expect_unusable(&handler, 1200u, 500u, OBSERVATION_STALE);
}

static void test_time_rollover(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    const vision_command_t command = {42u, 10u, true, 0, 0};
    CHECK(command_handler_accept(&handler, &command, UINT32_MAX - 99u) == COMMAND_ACCEPTED);
    vision_command_t output = {0};
    CHECK(command_handler_observation(&handler, 399u, 500u, &output) == OBSERVATION_VALID);
    CHECK(commands_equal(&output, &command));
    expect_unusable(&handler, 400u, 500u, OBSERVATION_STALE);
}

static void test_target_loss_and_reacquisition(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    vision_command_t command = {42u, 10u, true, 5000, -2000};
    CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
    command.sequence++;
    command.target_detected = false;
    CHECK(command_handler_accept(&handler, &command, 200u) == COMMAND_ACCEPTED);
    CHECK(handler.received_ms == 200u);
    expect_unusable(&handler, 201u, 500u, OBSERVATION_TARGET_LOST);
    /* Once communication expires, stale takes precedence over target lost. */
    expect_unusable(&handler, 700u, 500u, OBSERVATION_STALE);
    command.sequence++;
    command.target_detected = true;
    command.pan_error_mdeg = 1000;
    CHECK(command_handler_accept(&handler, &command, 800u) == COMMAND_ACCEPTED);
    vision_command_t output = {0};
    CHECK(command_handler_observation(&handler, 800u, 500u, &output) == OBSERVATION_VALID);
    CHECK(commands_equal(&output, &command));
}

static void test_session_reset(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    vision_command_t command = {42u, 100u, true, 5000, -2000};
    CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
    expect_unusable(&handler, 600u, 500u, OBSERVATION_STALE);
    const command_handler_t saved = handler;
    command.session = 43u;
    command.sequence = 1u;
    CHECK(command_handler_accept(&handler, &command, 700u) == COMMAND_SESSION_MISMATCH);
    CHECK(handlers_equal(&handler, &saved));
    /* The application is responsible for allowing this reset only while disabled. */
    command_handler_init(&handler);
    expect_unusable(&handler, 701u, 500u, OBSERVATION_NONE);
    CHECK(command_handler_accept(&handler, &command, 702u) == COMMAND_ACCEPTED);
    CHECK(commands_equal(&handler.latest, &command));
}

static void test_invalid_arguments(void)
{
    command_handler_init(NULL); /* Documented defensive no-op in implementation. */
    command_handler_t handler;
    command_handler_init(&handler);
    const vision_command_t command = {42u, 10u, true, 0, 0};
    const command_handler_t empty = handler;
    CHECK(command_handler_accept(NULL, &command, 100u) == COMMAND_INVALID_ARGUMENT);
    CHECK(command_handler_accept(&handler, NULL, 100u) == COMMAND_INVALID_ARGUMENT);
    CHECK(handlers_equal(&handler, &empty));
    CHECK(command_handler_accept(&handler, &command, 100u) == COMMAND_ACCEPTED);
    const command_handler_t saved = handler;
    CHECK(command_handler_accept(&handler, NULL, 200u) == COMMAND_INVALID_ARGUMENT);
    expect_unusable(NULL, 100u, 500u, OBSERVATION_NONE);
    expect_unusable(&handler, 100u, 0u, OBSERVATION_NONE);
    expect_unusable(&handler, 100u, 0x80000000u, OBSERVATION_NONE);
    expect_unusable(&handler, 100u, UINT32_MAX, OBSERVATION_NONE);
    CHECK(command_handler_observation(&handler, 100u, 500u, NULL) == OBSERVATION_NONE);
    CHECK(handlers_equal(&handler, &saved));
}

static void test_parser_integration(void)
{
    command_handler_t handler;
    command_handler_init(&handler);
    const char message[] = "V1 VISION 42 10 1 5000 -2000";
    vision_command_t parsed = {0};
    CHECK(protocol_parse_command(message, sizeof(message) - 1, &parsed) == PROTOCOL_OK);
    CHECK(command_handler_accept(&handler, &parsed, 100u) == COMMAND_ACCEPTED);
    vision_command_t output = {0};
    CHECK(command_handler_observation(&handler, 101u, 500u, &output) == OBSERVATION_VALID);
    CHECK(output.pan_error_mdeg == 5000 && output.tilt_error_mdeg == -2000);
    const command_handler_t saved = handler;
    const char invalid[] = "V1 VISION 42 11 2 0 0";
    protocol_result_t result = protocol_parse_command(invalid, sizeof(invalid) - 1, &parsed);
    CHECK(result == PROTOCOL_INVALID_MESSAGE);
    /* This is the caller's gate: never forward failed parses to the handler. */
    if (result == PROTOCOL_OK) {
        (void)command_handler_accept(&handler, &parsed, 500u);
    }
    CHECK(handlers_equal(&handler, &saved));
    expect_unusable(&handler, 600u, 500u, OBSERVATION_STALE);
}

int main(void)
{
    test_initialization();
    test_acceptance();
    test_rejection_preserves_state();
    test_sequence_boundaries();
    test_timeout_and_refresh();
    test_time_rollover();
    test_target_loss_and_reacquisition();
    test_session_reset();
    test_invalid_arguments();
    test_parser_integration();
    printf("PASS: 10 command-handler test groups (%u checks)\n", checks);
    return EXIT_SUCCESS;
}
