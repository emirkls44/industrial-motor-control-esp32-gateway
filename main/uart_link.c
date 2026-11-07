#include "uart_link.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>

#define RX_BUFFER_SIZE   1024
#define READ_CHUNK_SIZE  64
#define READ_TIMEOUT_MS  20
#define TX_LOCK_TIMEOUT  pdMS_TO_TICKS(100)
#define RX_TASK_STACK    4096
#define RX_TASK_PRIORITY 10

static const char *TAG = "UART_LINK";

static int s_port = -1;
static UartLinkTelemetryCallback_t s_callback = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static uint8_t s_tx_sequence = 0U;

static ProtocolParser_t s_parser;
static UartLinkStats_t s_stats;
static bool s_have_last_seq = false;
static uint8_t s_last_seq = 0U;

static void track_sequence(uint8_t seq)
{
    if (s_have_last_seq)
    {
        const uint8_t expected = (uint8_t)(s_last_seq + 1U);

        if (seq != expected)
        {
            const uint8_t missed = (uint8_t)(seq - expected);
            s_stats.lost_frames += missed;
            ESP_LOGW(TAG, "Telemetry gap: expected seq %u, got %u (%u lost)", expected, seq,
                     missed);
        }
    }

    s_last_seq = seq;
    s_have_last_seq = true;
}

static void rx_task(void *argument)
{
    (void)argument;

    uint8_t chunk[READ_CHUNK_SIZE];
    ProtocolFrame_t frame;
    ProtocolTelemetry_t telemetry;

    for (;;)
    {
        const int length =
            uart_read_bytes(s_port, chunk, sizeof(chunk), pdMS_TO_TICKS(READ_TIMEOUT_MS));

        for (int i = 0; i < length; i++)
        {
            if (!Protocol_ParserFeed(&s_parser, chunk[i], &frame))
            {
                continue;
            }

            if (!Protocol_DecodeTelemetry(&frame, &telemetry))
            {
                ESP_LOGW(TAG, "Ignoring frame type 0x%02x", frame.type);
                continue;
            }

            s_stats.telemetry_frames++;
            track_sequence(frame.seq);

            if (s_callback != NULL)
            {
                s_callback(&telemetry, frame.seq);
            }
        }

        s_stats.crc_errors = s_parser.crc_errors;
        s_stats.format_errors = s_parser.format_errors;
    }
}

esp_err_t UartLink_Init(const UartLinkConfig_t *config, UartLinkTelemetryCallback_t callback)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t uart_config = {
        .baud_rate = config->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(config->port, &uart_config), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(uart_set_pin(config->port, config->tx_pin, config->rx_pin,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin");
    ESP_RETURN_ON_ERROR(uart_driver_install(config->port, RX_BUFFER_SIZE, 0, 0, NULL, 0), TAG,
                        "uart_driver_install");

    s_port = config->port;
    s_callback = callback;
    Protocol_ParserInit(&s_parser);

    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(rx_task, "uart_link_rx", RX_TASK_STACK, NULL, RX_TASK_PRIORITY, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d ready (TX=GPIO%d, RX=GPIO%d, %d baud)", config->port, config->tx_pin,
             config->rx_pin, config->baudrate);

    return ESP_OK;
}

esp_err_t UartLink_SendCommand(ProtocolCommand_t command, uint8_t value)
{
    if ((s_port < 0) || (s_tx_mutex == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_tx_mutex, TX_LOCK_TIMEOUT) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    /* Encoding and writing under one lock keeps sequence numbers in order
     * when the MQTT task and the heartbeat task send at the same time. */
    uint8_t frame[PROTOCOL_COMMAND_FRAME_SIZE];
    const size_t length =
        Protocol_EncodeCommand(command, value, s_tx_sequence, frame, sizeof(frame));
    esp_err_t result = ESP_FAIL;

    if ((length > 0U) && (uart_write_bytes(s_port, frame, length) == (int)length))
    {
        s_tx_sequence++;
        result = ESP_OK;
    }

    xSemaphoreGive(s_tx_mutex);

    if ((result == ESP_OK) && (command != PROTOCOL_CMD_HEARTBEAT))
    {
        ESP_LOGI(TAG, "Command sent: id=%u value=%u", (unsigned)command, value);
    }

    return result;
}

UartLinkStats_t UartLink_GetStats(void)
{
    return s_stats;
}
