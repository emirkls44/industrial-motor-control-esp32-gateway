/*
 * ESP32 gateway between the STM32 motor controller (UART) and an MQTT broker.
 *
 *   STM32 --UART telemetry--> ESP32 --MQTT DEVICE_ID/telemetry--> broker
 *   STM32 <--UART commands--- ESP32 <--MQTT DEVICE_ID/command---- broker
 *
 * Settings (Wi-Fi, broker, pins) are in `idf.py menuconfig` ->
 * "Motor gateway configuration". They are stored in sdkconfig, which is not
 * committed.
 */
#include "command_parser.h"
#include "mqtt_manager.h"
#include "protocol.h"
#include "uart_link.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define WIFI_WAIT_LOG_PERIOD_MS 10000U
#define HEARTBEAT_TASK_STACK    2048
#define HEARTBEAT_TASK_PRIORITY 5

static const char *TAG = "GATEWAY";

static void on_telemetry(const ProtocolTelemetry_t *telemetry, uint8_t seq)
{
    const UartLinkStats_t stats = UartLink_GetStats();

    MQTTManager_PublishTelemetry(telemetry, seq, stats.lost_frames);

    ESP_LOGD(TAG, "seq=%u state=%u duty=%u/%u flags=0x%02x adc=%u", seq, telemetry->state,
             telemetry->current_duty, telemetry->target_duty, telemetry->flags, telemetry->adc_raw);
}

static void on_mqtt_command(const char *text)
{
    ProtocolCommand_t command;
    uint8_t value;

    if (!CommandParser_Parse(text, &command, &value))
    {
        ESP_LOGW(TAG, "Unknown command '%s' (expected START, STOP or DUTY <0-100>)", text);
        return;
    }

    const esp_err_t result = UartLink_SendCommand(command, value);

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to forward command: %s", esp_err_to_name(result));
    }
}

/* The STM32 stops a remotely started motor if it hears nothing for 2 s.
 * Heartbeats are only sent while the broker connection is up, so losing the
 * remote side (Wi-Fi, broker, or this ESP32) stops the motor. */
static void heartbeat_task(void *argument)
{
    (void)argument;

    const TickType_t period = pdMS_TO_TICKS(CONFIG_GATEWAY_HEARTBEAT_PERIOD_MS);

    for (;;)
    {
        if (MQTTManager_IsConnected())
        {
            (void)UartLink_SendCommand(PROTOCOL_CMD_HEARTBEAT, 0U);
        }

        vTaskDelay(period);
    }
}

void app_main(void)
{
    const UartLinkConfig_t uart_config = {
        .port = CONFIG_GATEWAY_UART_PORT,
        .tx_pin = CONFIG_GATEWAY_UART_TX_PIN,
        .rx_pin = CONFIG_GATEWAY_UART_RX_PIN,
        .baudrate = CONFIG_GATEWAY_UART_BAUDRATE,
    };

    /* The UART starts first so telemetry is logged even without Wi-Fi. */
    ESP_ERROR_CHECK(UartLink_Init(&uart_config, on_telemetry));

    WiFiManager_Init(CONFIG_GATEWAY_WIFI_SSID, CONFIG_GATEWAY_WIFI_PASSWORD);

    while (!WiFiManager_WaitConnected(WIFI_WAIT_LOG_PERIOD_MS))
    {
        ESP_LOGW(TAG, "Still waiting for Wi-Fi '%s'...", CONFIG_GATEWAY_WIFI_SSID);
    }

    const MQTTManagerConfig_t mqtt_config = {
        .broker_uri = CONFIG_GATEWAY_MQTT_BROKER_URI,
        .username = CONFIG_GATEWAY_MQTT_USERNAME,
        .password = CONFIG_GATEWAY_MQTT_PASSWORD,
        .device_id = CONFIG_GATEWAY_DEVICE_ID,
    };

    MQTTManager_Init(&mqtt_config, on_mqtt_command);

    if (xTaskCreate(heartbeat_task, "heartbeat", HEARTBEAT_TASK_STACK, NULL,
                    HEARTBEAT_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Could not start the heartbeat task");
    }
}
