/*
 * Host unit tests for the MQTT text command parser (main/command_parser.c).
 * Build and run with `make -C tests`.
 */
#include "command_parser.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

static void expect(const char *text, ProtocolCommand_t expected_command, uint8_t expected_value)
{
    ProtocolCommand_t command = (ProtocolCommand_t)0;
    uint8_t value = 0xFF;

    const bool ok = CommandParser_Parse(text, &command, &value);

    if (!ok || (command != expected_command) || (value != expected_value))
    {
        printf("  FAIL parse(\"%s\") -> ok=%d command=%d value=%u\n", text, ok, (int)command,
               value);
        g_failures++;
    }
}

static void expect_rejected(const char *text)
{
    ProtocolCommand_t command;
    uint8_t value;

    if (CommandParser_Parse(text, &command, &value))
    {
        printf("  FAIL parse(\"%s\") should be rejected\n", text);
        g_failures++;
    }
}

int main(void)
{
    expect("START", PROTOCOL_CMD_START, 0);
    expect("STOP", PROTOCOL_CMD_STOP, 0);
    expect("start", PROTOCOL_CMD_START, 0);
    expect("  Stop\r\n", PROTOCOL_CMD_STOP, 0);
    expect("DUTY 0", PROTOCOL_CMD_SET_DUTY, 0);
    expect("DUTY 70", PROTOCOL_CMD_SET_DUTY, 70);
    expect("duty   100\n", PROTOCOL_CMD_SET_DUTY, 100);

    expect_rejected("");
    expect_rejected("   ");
    expect_rejected("STARTX");
    expect_rejected("START NOW");
    expect_rejected("DUTY");
    expect_rejected("DUTY ");
    expect_rejected("DUTY 101");
    expect_rejected("DUTY 1000");
    expect_rejected("DUTY -5");
    expect_rejected("DUTY 5O");
    expect_rejected("DUTY70");
    expect_rejected("HEARTBEAT"); /* internal command, not accepted from MQTT */
    expect_rejected("DUTY 99999999999999999999");
    expect_rejected("A very long string that does not fit in the buffer at all");

    CHECK(!CommandParser_Parse(NULL, &(ProtocolCommand_t){0}, &(uint8_t){0}));

    printf("%s command_parser\n\n%d failure(s)\n", (g_failures == 0) ? "PASS" : "FAIL", g_failures);
    return (g_failures == 0) ? 0 : 1;
}
