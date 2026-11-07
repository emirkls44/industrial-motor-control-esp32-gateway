/*
 * Host unit tests for the shared protocol module (main/protocol.c).
 * Build and run with `make -C tests`.
 */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

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

/* Feeds a buffer into the parser and returns how many frames were decoded.
 * The last decoded frame is stored in `last`. */
static int feed(ProtocolParser_t *parser, const uint8_t *data, size_t length, ProtocolFrame_t *last)
{
    int frames = 0;

    for (size_t i = 0; i < length; i++)
    {
        if (Protocol_ParserFeed(parser, data[i], last))
        {
            frames++;
        }
    }

    return frames;
}

static void test_crc8_check_value(void)
{
    /* Standard check value of CRC-8/SMBUS */
    const uint8_t data[] = "123456789";
    CHECK(Protocol_Crc8(data, 9) == 0xF4);
}

static void test_telemetry_layout(void)
{
    const ProtocolTelemetry_t telemetry = {.state = 2,
                                           .command_duty = 60,
                                           .target_duty = 60,
                                           .current_duty = 51,
                                           .flags = PROTOCOL_FLAG_REMOTE,
                                           .adc_raw = 0x0ABC};
    uint8_t frame[PROTOCOL_MAX_FRAME_SIZE];

    const size_t size = Protocol_EncodeTelemetry(&telemetry, 7, frame, sizeof(frame));

    CHECK(size == PROTOCOL_TELEMETRY_FRAME_SIZE);
    CHECK(size == 14);

    const uint8_t expected[13] = {0xAA,
                                  0x55,
                                  PROTOCOL_VERSION,
                                  PROTOCOL_MSG_TELEMETRY,
                                  7,
                                  7,
                                  2,
                                  60,
                                  60,
                                  51,
                                  PROTOCOL_FLAG_REMOTE,
                                  0xBC,
                                  0x0A};
    CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
    CHECK(frame[13] == Protocol_Crc8(&frame[2], 11));
}

static void test_telemetry_round_trip(void)
{
    const ProtocolTelemetry_t sent = {.state = 1,
                                      .command_duty = 80,
                                      .target_duty = 80,
                                      .current_duty = 45,
                                      .flags = PROTOCOL_FLAG_ESTOP,
                                      .adc_raw = 4095};
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE];
    const size_t size = Protocol_EncodeTelemetry(&sent, 200, buffer, sizeof(buffer));

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    ProtocolTelemetry_t received;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, buffer, size, &frame) == 1);
    CHECK(frame.seq == 200);
    CHECK(Protocol_DecodeTelemetry(&frame, &received));
    CHECK(memcmp(&sent, &received, sizeof(sent)) == 0);
    CHECK(!Protocol_DecodeCommand(&frame, &(ProtocolCommand_t){0}, &(uint8_t){0}));
}

static void test_command_round_trip(void)
{
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE];
    const size_t size =
        Protocol_EncodeCommand(PROTOCOL_CMD_SET_DUTY, 75, 3, buffer, sizeof(buffer));
    CHECK(size == PROTOCOL_COMMAND_FRAME_SIZE);

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    ProtocolCommand_t command;
    uint8_t value;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, buffer, size, &frame) == 1);
    CHECK(Protocol_DecodeCommand(&frame, &command, &value));
    CHECK(command == PROTOCOL_CMD_SET_DUTY);
    CHECK(value == 75);
}

static void test_command_validation(void)
{
    ProtocolFrame_t frame = {.type = PROTOCOL_MSG_COMMAND, .length = 2};
    ProtocolCommand_t command;
    uint8_t value;

    frame.payload[0] = PROTOCOL_CMD_SET_DUTY;
    frame.payload[1] = 101;
    CHECK(!Protocol_DecodeCommand(&frame, &command, &value));

    frame.payload[0] = 0x7F; /* unknown command */
    frame.payload[1] = 0;
    CHECK(!Protocol_DecodeCommand(&frame, &command, &value));

    frame.payload[0] = PROTOCOL_CMD_HEARTBEAT;
    CHECK(Protocol_DecodeCommand(&frame, &command, &value));
    CHECK(command == PROTOCOL_CMD_HEARTBEAT);
}

static void test_resync_after_garbage(void)
{
    uint8_t stream[64];
    size_t n = 0;

    /* Noise, then a doubled start byte, then a valid frame */
    const uint8_t noise[] = {0x00, 0x55, 0xAA, 0x12, 0xFF, 0xAA};
    memcpy(&stream[n], noise, sizeof(noise));
    n += sizeof(noise);
    n += Protocol_EncodeCommand(PROTOCOL_CMD_START, 0, 1, &stream[n], sizeof(stream) - n);

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, stream, n, &frame) == 1);
    CHECK(frame.type == PROTOCOL_MSG_COMMAND);
    CHECK(frame.payload[0] == PROTOCOL_CMD_START);
}

static void test_crc_error_is_rejected_and_parser_recovers(void)
{
    uint8_t stream[64];
    size_t n = Protocol_EncodeCommand(PROTOCOL_CMD_START, 0, 1, stream, sizeof(stream));
    stream[n - 1] ^= 0x01; /* corrupt CRC */
    n += Protocol_EncodeCommand(PROTOCOL_CMD_STOP, 0, 2, &stream[n], sizeof(stream) - n);

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, stream, n, &frame) == 1);
    CHECK(parser.crc_errors == 1);
    CHECK(frame.payload[0] == PROTOCOL_CMD_STOP);
}

static void test_bad_header_is_rejected(void)
{
    uint8_t stream[64];
    size_t n = 0;

    /* Wrong protocol version (old v1 command frame) */
    const uint8_t old_frame[] = {0xAA, 0x55, 0x01, 0x02, 0x02, 0x01, 0x00, 0x00};
    memcpy(&stream[n], old_frame, sizeof(old_frame));
    n += sizeof(old_frame);

    /* Length larger than the maximum payload */
    const uint8_t too_long[] = {0xAA, 0x55, PROTOCOL_VERSION, 0x02, PROTOCOL_MAX_PAYLOAD + 1};
    memcpy(&stream[n], too_long, sizeof(too_long));
    n += sizeof(too_long);

    n += Protocol_EncodeCommand(PROTOCOL_CMD_HEARTBEAT, 0, 9, &stream[n], sizeof(stream) - n);

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, stream, n, &frame) == 1);
    CHECK(parser.format_errors == 2);
    CHECK(frame.seq == 9);
}

static void test_back_to_back_frames(void)
{
    uint8_t stream[128];
    size_t n = 0;

    for (uint8_t i = 0; i < 5; i++)
    {
        const ProtocolTelemetry_t t = {.state = i, .adc_raw = (uint16_t)(i * 1000U)};
        n += Protocol_EncodeTelemetry(&t, i, &stream[n], sizeof(stream) - n);
    }

    ProtocolParser_t parser;
    ProtocolFrame_t frame;
    Protocol_ParserInit(&parser);

    CHECK(feed(&parser, stream, n, &frame) == 5);
    CHECK(frame.seq == 4);
}

static void test_encoder_rejects_small_buffer(void)
{
    uint8_t small[PROTOCOL_TELEMETRY_FRAME_SIZE - 1];
    const ProtocolTelemetry_t t = {0};

    CHECK(Protocol_EncodeTelemetry(&t, 0, small, sizeof(small)) == 0);
    CHECK(Protocol_EncodeTelemetry(NULL, 0, small, sizeof(small)) == 0);
}

int main(void)
{
    struct
    {
        const char *name;
        void (*run)(void);
    } const tests[] = {
        {"crc8_check_value", test_crc8_check_value},
        {"telemetry_layout", test_telemetry_layout},
        {"telemetry_round_trip", test_telemetry_round_trip},
        {"command_round_trip", test_command_round_trip},
        {"command_validation", test_command_validation},
        {"resync_after_garbage", test_resync_after_garbage},
        {"crc_error_is_rejected_and_parser_recovers",
         test_crc_error_is_rejected_and_parser_recovers},
        {"bad_header_is_rejected", test_bad_header_is_rejected},
        {"back_to_back_frames", test_back_to_back_frames},
        {"encoder_rejects_small_buffer", test_encoder_rejects_small_buffer},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        const int before = g_failures;
        tests[i].run();
        printf("%s %s\n", (g_failures == before) ? "PASS" : "FAIL", tests[i].name);
    }

    printf("\n%d failure(s)\n", g_failures);
    return (g_failures == 0) ? 0 : 1;
}
