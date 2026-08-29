#include "mqtt_manager.h"

#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Connected to broker");
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected from broker");
            break;

        default:
            break;
    }
}

void MQTTManager_Init(const char *broker_uri)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(s_client,
                                       ESP_EVENT_ANY_ID,
                                       mqtt_event_handler,
                                       NULL)
    );

    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
}

bool MQTTManager_IsConnected(void)
{
    return s_connected;
}

void MQTTManager_PublishTelemetry(uint8_t state,
                                  uint8_t command_duty,
                                  uint8_t target_duty,
                                  uint8_t current_duty,
                                  uint8_t estop,
                                  uint16_t adc_raw)
{
    if ((s_client == NULL) || !s_connected)
    {
        return;
    }

    char payload[160];

    snprintf(payload,
             sizeof(payload),
             "{\"state\":%u,\"command_duty\":%u,\"target_duty\":%u,"
             "\"current_duty\":%u,\"estop\":%u,\"adc_raw\":%u}",
             state,
             command_duty,
             target_duty,
             current_duty,
             estop,
             adc_raw);

    esp_mqtt_client_publish(s_client,
                            "motor01/telemetry",
                            payload,
                            0,
                            0,
                            0);
}
