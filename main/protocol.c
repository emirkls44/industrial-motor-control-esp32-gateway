#include "protocol.h"

#include <string.h>

/* Offsets inside a frame */
#define OFFSET_SOF_1   0U
#define OFFSET_SOF_2   1U
#define OFFSET_VERSION 2U
#define OFFSET_TYPE    3U
#define OFFSET_LENGTH  4U
#define OFFSET_SEQ     5U
#define OFFSET_PAYLOAD 6U

#define MAX_DUTY_PERCENT 100U

uint8_t Protocol_Crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x00U;

    for (size_t i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1U) ^ 0x07U);
            }
            else
            {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return crc;
}

size_t Protocol_EncodeFrame(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t length,
                            uint8_t *out, size_t out_size)
{
    const size_t frame_size = (size_t)PROTOCOL_HEADER_SIZE + length + PROTOCOL_CRC_SIZE;

    if ((out == NULL) || (length > PROTOCOL_MAX_PAYLOAD) || (out_size < frame_size) ||
        ((payload == NULL) && (length > 0U)))
    {
        return 0U;
    }

    out[OFFSET_SOF_1] = PROTOCOL_SOF_1;
    out[OFFSET_SOF_2] = PROTOCOL_SOF_2;
    out[OFFSET_VERSION] = PROTOCOL_VERSION;
    out[OFFSET_TYPE] = type;
    out[OFFSET_LENGTH] = length;
    out[OFFSET_SEQ] = seq;

    if (length > 0U)
    {
        memcpy(&out[OFFSET_PAYLOAD], payload, length);
    }

    /* CRC covers VERSION..PAYLOAD */
    out[frame_size - 1U] = Protocol_Crc8(&out[OFFSET_VERSION], frame_size - 3U);

    return frame_size;
}

size_t Protocol_EncodeTelemetry(const ProtocolTelemetry_t *telemetry, uint8_t seq, uint8_t *out,
                                size_t out_size)
{
    if (telemetry == NULL)
    {
        return 0U;
    }

    const uint8_t payload[PROTOCOL_TELEMETRY_PAYLOAD_SIZE] = {
        telemetry->state,
        telemetry->command_duty,
        telemetry->target_duty,
        telemetry->current_duty,
        telemetry->flags,
        (uint8_t)(telemetry->adc_raw & 0xFFU),
        (uint8_t)((telemetry->adc_raw >> 8U) & 0xFFU),
    };

    return Protocol_EncodeFrame(PROTOCOL_MSG_TELEMETRY, seq, payload, sizeof(payload), out,
                                out_size);
}

size_t Protocol_EncodeCommand(ProtocolCommand_t command, uint8_t value, uint8_t seq, uint8_t *out,
                              size_t out_size)
{
    const uint8_t payload[PROTOCOL_COMMAND_PAYLOAD_SIZE] = {(uint8_t)command, value};

    return Protocol_EncodeFrame(PROTOCOL_MSG_COMMAND, seq, payload, sizeof(payload), out, out_size);
}

bool Protocol_DecodeTelemetry(const ProtocolFrame_t *frame, ProtocolTelemetry_t *telemetry)
{
    if ((frame == NULL) || (telemetry == NULL) || (frame->type != PROTOCOL_MSG_TELEMETRY) ||
        (frame->length != PROTOCOL_TELEMETRY_PAYLOAD_SIZE))
    {
        return false;
    }

    telemetry->state = frame->payload[0];
    telemetry->command_duty = frame->payload[1];
    telemetry->target_duty = frame->payload[2];
    telemetry->current_duty = frame->payload[3];
    telemetry->flags = frame->payload[4];
    telemetry->adc_raw = (uint16_t)(frame->payload[5] | ((uint16_t)frame->payload[6] << 8U));

    return true;
}

bool Protocol_DecodeCommand(const ProtocolFrame_t *frame, ProtocolCommand_t *command,
                            uint8_t *value)
{
    if ((frame == NULL) || (command == NULL) || (value == NULL) ||
        (frame->type != PROTOCOL_MSG_COMMAND) || (frame->length != PROTOCOL_COMMAND_PAYLOAD_SIZE))
    {
        return false;
    }

    const uint8_t id = frame->payload[0];
    const uint8_t argument = frame->payload[1];

    switch (id)
    {
    case PROTOCOL_CMD_START:
    case PROTOCOL_CMD_STOP:
    case PROTOCOL_CMD_HEARTBEAT:
        break;

    case PROTOCOL_CMD_SET_DUTY:
        if (argument > MAX_DUTY_PERCENT)
        {
            return false;
        }
        break;

    default:
        return false;
    }

    *command = (ProtocolCommand_t)id;
    *value = argument;

    return true;
}

void Protocol_ParserInit(ProtocolParser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    memset(parser, 0, sizeof(*parser));
}

/* Restart the search for a frame. If the byte that caused the failure is itself
 * a start byte, keep it so that "AA AA 55 ..." still synchronises. */
static void parser_restart(ProtocolParser_t *parser, uint8_t byte)
{
    if (byte == PROTOCOL_SOF_1)
    {
        parser->buffer[OFFSET_SOF_1] = byte;
        parser->index = 1U;
    }
    else
    {
        parser->index = 0U;
    }
}

bool Protocol_ParserFeed(ProtocolParser_t *parser, uint8_t byte, ProtocolFrame_t *frame)
{
    if ((parser == NULL) || (frame == NULL))
    {
        return false;
    }

    switch (parser->index)
    {
    case OFFSET_SOF_1:
        parser_restart(parser, byte);
        return false;

    case OFFSET_SOF_2:
        if (byte == PROTOCOL_SOF_2)
        {
            parser->buffer[OFFSET_SOF_2] = byte;
            parser->index = 2U;
        }
        else
        {
            parser_restart(parser, byte);
        }
        return false;

    default:
        break;
    }

    parser->buffer[parser->index] = byte;
    parser->index++;

    if (parser->index == (OFFSET_LENGTH + 1U))
    {
        /* VERSION, TYPE and LEN are known: validate before buffering the payload */
        if ((parser->buffer[OFFSET_VERSION] != PROTOCOL_VERSION) ||
            (parser->buffer[OFFSET_LENGTH] > PROTOCOL_MAX_PAYLOAD))
        {
            parser->format_errors++;
            parser_restart(parser, byte);
            return false;
        }

        parser->expected_size =
            (uint8_t)(PROTOCOL_HEADER_SIZE + parser->buffer[OFFSET_LENGTH] + PROTOCOL_CRC_SIZE);
    }

    if ((parser->index <= OFFSET_LENGTH) || (parser->index < parser->expected_size))
    {
        return false;
    }

    /* Complete frame received */
    const uint8_t size = parser->expected_size;
    parser->index = 0U;

    const uint8_t crc = Protocol_Crc8(&parser->buffer[OFFSET_VERSION], (size_t)size - 3U);

    if (crc != parser->buffer[size - 1U])
    {
        parser->crc_errors++;
        return false;
    }

    frame->type = parser->buffer[OFFSET_TYPE];
    frame->length = parser->buffer[OFFSET_LENGTH];
    frame->seq = parser->buffer[OFFSET_SEQ];
    memcpy(frame->payload, &parser->buffer[OFFSET_PAYLOAD], frame->length);

    return true;
}
