#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * Serial protocol between the STM32 motor controller and the ESP32 gateway.
 *
 * Frame layout (multi-byte fields are little-endian):
 *
 *   +------+------+---------+------+-----+-----+--------------+------+
 *   | SOF1 | SOF2 | VERSION | TYPE | LEN | SEQ | PAYLOAD[LEN] | CRC8 |
 *   | 0xAA | 0x55 |  0x02   |      |     |     |              |      |
 *   +------+------+---------+------+-----+-----+--------------+------+
 *
 * CRC8 is CRC-8/SMBUS (poly 0x07, init 0x00) computed over VERSION..PAYLOAD.
 *
 * This module is shared with the ESP32 gateway repository. It must not depend
 * on any MCU or HAL header so that it can be built and unit tested on a PC
 * (see tests/). Keep both copies identical.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_SOF_1   0xAAU
#define PROTOCOL_SOF_2   0x55U
#define PROTOCOL_VERSION 0x02U

#define PROTOCOL_HEADER_SIZE    6U /* SOF1 SOF2 VERSION TYPE LEN SEQ */
#define PROTOCOL_CRC_SIZE       1U
#define PROTOCOL_MAX_PAYLOAD    16U
#define PROTOCOL_MAX_FRAME_SIZE (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD + PROTOCOL_CRC_SIZE)

#define PROTOCOL_TELEMETRY_PAYLOAD_SIZE 7U
#define PROTOCOL_COMMAND_PAYLOAD_SIZE   2U
#define PROTOCOL_TELEMETRY_FRAME_SIZE                                                              \
    (PROTOCOL_HEADER_SIZE + PROTOCOL_TELEMETRY_PAYLOAD_SIZE + PROTOCOL_CRC_SIZE)
#define PROTOCOL_COMMAND_FRAME_SIZE                                                                \
    (PROTOCOL_HEADER_SIZE + PROTOCOL_COMMAND_PAYLOAD_SIZE + PROTOCOL_CRC_SIZE)

typedef enum
{
    PROTOCOL_MSG_TELEMETRY = 0x01, /* STM32 -> ESP32 */
    PROTOCOL_MSG_COMMAND = 0x02    /* ESP32 -> STM32 */
} ProtocolMsgType_t;

typedef enum
{
    PROTOCOL_CMD_START = 0x01,
    PROTOCOL_CMD_STOP = 0x02,
    PROTOCOL_CMD_SET_DUTY = 0x03, /* value: 0..100 % */
    PROTOCOL_CMD_HEARTBEAT = 0x04 /* keeps the link alive, no action */
} ProtocolCommand_t;

/* Bits of ProtocolTelemetry_t.flags */
#define PROTOCOL_FLAG_ESTOP          (1U << 0) /* emergency stop input is active        */
#define PROTOCOL_FLAG_REMOTE         (1U << 1) /* motor is under remote (MQTT) control  */
#define PROTOCOL_FLAG_LINK_LOST      (1U << 2) /* stopped because the link timed out    */
#define PROTOCOL_FLAG_WATCHDOG_RESET (1U << 3) /* last MCU reset was caused by the IWDG */

typedef struct
{
    uint8_t state;        /* MotorState_t                                  */
    uint8_t command_duty; /* duty requested by the active source, 0..100 % */
    uint8_t target_duty;  /* duty after limits, 0 or MIN..100 %            */
    uint8_t current_duty; /* duty currently applied to the PWM output      */
    uint8_t flags;        /* PROTOCOL_FLAG_*                               */
    uint16_t adc_raw;     /* potentiometer, 12-bit                         */
} ProtocolTelemetry_t;

typedef struct
{
    uint8_t type;
    uint8_t seq;
    uint8_t length;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} ProtocolFrame_t;

/* Byte-by-byte frame parser. Safe to feed from an interrupt. */
typedef struct
{
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE];
    uint8_t index;
    uint8_t expected_size;
    uint16_t crc_errors;
    uint16_t format_errors;
} ProtocolParser_t;

uint8_t Protocol_Crc8(const uint8_t *data, size_t length);

/* Encoders return the number of bytes written to `out`, or 0 on error. */
size_t Protocol_EncodeFrame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t length,
                            uint8_t *out, size_t out_size);
size_t Protocol_EncodeTelemetry(const ProtocolTelemetry_t *telemetry, uint8_t seq, uint8_t *out,
                                size_t out_size);
size_t Protocol_EncodeCommand(ProtocolCommand_t command, uint8_t value, uint8_t seq, uint8_t *out,
                              size_t out_size);

/* Decoders return false if the frame has the wrong type, length or values. */
bool Protocol_DecodeTelemetry(const ProtocolFrame_t *frame, ProtocolTelemetry_t *telemetry);
bool Protocol_DecodeCommand(const ProtocolFrame_t *frame, ProtocolCommand_t *command,
                            uint8_t *value);

void Protocol_ParserInit(ProtocolParser_t *parser);

/* Feeds one received byte. Returns true and fills `frame` when a complete
 * frame with a valid header and CRC has been received. */
bool Protocol_ParserFeed(ProtocolParser_t *parser, uint8_t byte, ProtocolFrame_t *frame);

#endif /* PROTOCOL_H */
