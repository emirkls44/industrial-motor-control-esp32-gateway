#include "mqtt_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define TELEMETRY_TOPIC "motor01/telemetry"
#define COMMAND_TOPIC   "motor01/command"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static MQTTCommandCallback_t s_command_callback = NULL;

void MQTTManager_SetCommandCallback(MQTTCommandCallback_t callback)
{
    s_command_callback = callback;
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Connected to broker");

            esp_mqtt_client_subscribe(s_client, COMMAND_TOPIC, 0);
            ESP_LOGI(TAG, "Subscribed: %s", COMMAND_TOPIC);
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected from broker");
            break;

        case MQTT_EVENT_DATA:
        {
            if ((event->topic_len == (int)strlen(COMMAND_TOPIC)) &&
                (strncmp(event->topic, COMMAND_TOPIC, event->topic_len) == 0))
            {
                char command[32];
                int copy_len = event->data_len;

                if (copy_len >= (int)sizeof(command))
                {
                    copy_len = sizeof(command) - 1;
                }

                memcpy(command, event->data, copy_len);
                command[copy_len] = '\0';

                ESP_LOGI(TAG, "Command received: %s", command);

                if (s_command_callback != NULL)
                {
                    s_command_callback(command);
                }
            }
            break;
        }

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
                            TELEMETRY_TOPIC,
                            payload,
                            0,
                            0,
                            0);
}
