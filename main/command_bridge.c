#include "command_bridge.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#define UART_PORT UART_NUM_2

#define FRAME_HEADER_1 0xAA
#define FRAME_HEADER_2 0x55
#define PROTOCOL_VER   0x01
#define MSG_COMMAND    0x02
#define PAYLOAD_LEN    0x02

#define CMD_START      0x01
#define CMD_STOP       0x02
#define CMD_SET_DUTY   0x03

static const char *TAG = "CMD_BRIDGE";

static uint8_t crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0x00;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc <<= 1;
        }
    }

    return crc;
}

static void send_command(uint8_t command_id, uint8_t value)
{
    uint8_t frame[8];

    frame[0] = FRAME_HEADER_1;
    frame[1] = FRAME_HEADER_2;
    frame[2] = PROTOCOL_VER;
    frame[3] = MSG_COMMAND;
    frame[4] = PAYLOAD_LEN;
    frame[5] = command_id;
    frame[6] = value;
    frame[7] = crc8(&frame[2], 5);

    uart_write_bytes(UART_PORT, (const char *)frame, sizeof(frame));

    ESP_LOGI(TAG, "UART command sent: id=%u value=%u",
             command_id, value);
}

void CommandBridge_Init(void)
{
    /* UART2 driver/configuration is already initialized in main.c. */
}

void CommandBridge_HandleMqttCommand(const char *command)
{
    if (strcmp(command, "START") == 0)
    {
        send_command(CMD_START, 0);
        return;
    }

    if (strcmp(command, "STOP") == 0)
    {
        send_command(CMD_STOP, 0);
        return;
    }

    unsigned int duty = 0;

    if ((sscanf(command, "DUTY %u", &duty) == 1) && (duty <= 100))
    {
        send_command(CMD_SET_DUTY, (uint8_t)duty);
        return;
    }

    ESP_LOGW(TAG, "Unknown command: %s", command);
}
