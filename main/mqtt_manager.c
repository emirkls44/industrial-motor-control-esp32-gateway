#include "mqtt_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define TOPIC_SIZE          64
#define COMMAND_MAX_LENGTH  32
#define TELEMETRY_JSON_SIZE 256
#define STATUS_ONLINE       "online"
#define STATUS_OFFLINE      "offline"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static atomic_bool s_connected = false;
static MQTTCommandCallback_t s_command_callback = NULL;

static char s_telemetry_topic[TOPIC_SIZE];
static char s_command_topic[TOPIC_SIZE];
static char s_status_topic[TOPIC_SIZE];

/* Names for MotorState_t on the STM32 side */
static const char *const STATE_NAMES[] = {"STOPPED", "STARTING", "RUNNING", "STOPPING"};

static const char *state_name(uint8_t state)
{
    return (state < (sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]))) ? STATE_NAMES[state]
                                                                    : "UNKNOWN";
}

static const char *json_bool(uint8_t flags, uint8_t mask)
{
    return ((flags & mask) != 0U) ? "true" : "false";
}

static void handle_data(const esp_mqtt_event_handle_t event)
{
    const size_t topic_length = strlen(s_command_topic);

    if ((event->topic_len != (int)topic_length) ||
        (strncmp(event->topic, s_command_topic, topic_length) != 0))
    {
        return;
    }

    /* Commands are a few bytes; ignore anything fragmented or too long. */
    if ((event->current_data_offset != 0) || (event->data_len != event->total_data_len) ||
        (event->data_len >= COMMAND_MAX_LENGTH))
    {
        ESP_LOGW(TAG, "Ignoring oversized command (%d bytes)", event->total_data_len);
        return;
    }

    char command[COMMAND_MAX_LENGTH];
    memcpy(command, event->data, (size_t)event->data_len);
    command[event->data_len] = '\0';

    ESP_LOGI(TAG, "Command received: %s", command);

    if (s_command_callback != NULL)
    {
        s_command_callback(command);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    const esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        atomic_store(&s_connected, true);
        ESP_LOGI(TAG, "Connected to broker");
        esp_mqtt_client_publish(s_client, s_status_topic, STATUS_ONLINE, 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, s_command_topic, 1);
        ESP_LOGI(TAG, "Subscribed to %s", s_command_topic);
        break;

    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "Disconnected from broker");
        break;

    case MQTT_EVENT_DATA:
        handle_data(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

void MQTTManager_Init(const MQTTManagerConfig_t *config, MQTTCommandCallback_t callback)
{
    if (config == NULL)
    {
        return;
    }

    s_command_callback = callback;

    snprintf(s_telemetry_topic, sizeof(s_telemetry_topic), "%s/telemetry", config->device_id);
    snprintf(s_command_topic, sizeof(s_command_topic), "%s/command", config->device_id);
    snprintf(s_status_topic, sizeof(s_status_topic), "%s/status", config->device_id);

    const bool use_credentials = (config->username != NULL) && (config->username[0] != '\0');

    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = config->broker_uri,
        .credentials.username = use_credentials ? config->username : NULL,
        .credentials.authentication.password = use_credentials ? config->password : NULL,
        .session.last_will =
            {
                .topic = s_status_topic,
                .msg = STATUS_OFFLINE,
                .qos = 1,
                .retain = 1,
            },
    };

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    if (!use_credentials)
    {
        ESP_LOGW(TAG, "No MQTT credentials configured: anyone on the network can send commands");
    }
}

bool MQTTManager_IsConnected(void)
{
    return atomic_load(&s_connected);
}

void MQTTManager_PublishTelemetry(const ProtocolTelemetry_t *telemetry, uint8_t seq,
                                  uint32_t lost_frames)
{
    if ((telemetry == NULL) || (s_client == NULL) || !MQTTManager_IsConnected())
    {
        return;
    }

    char payload[TELEMETRY_JSON_SIZE];

    const int length =
        snprintf(payload, sizeof(payload),
                 "{\"seq\":%u,\"state\":\"%s\",\"command_duty\":%u,\"target_duty\":%u,"
                 "\"current_duty\":%u,\"estop\":%s,\"remote\":%s,\"link_lost\":%s,"
                 "\"watchdog_reset\":%s,\"adc_raw\":%u,\"lost_frames\":%lu}",
                 seq, state_name(telemetry->state), telemetry->command_duty, telemetry->target_duty,
                 telemetry->current_duty, json_bool(telemetry->flags, PROTOCOL_FLAG_ESTOP),
                 json_bool(telemetry->flags, PROTOCOL_FLAG_REMOTE),
                 json_bool(telemetry->flags, PROTOCOL_FLAG_LINK_LOST),
                 json_bool(telemetry->flags, PROTOCOL_FLAG_WATCHDOG_RESET), telemetry->adc_raw,
                 (unsigned long)lost_frames);

    if ((length <= 0) || (length >= (int)sizeof(payload)))
    {
        ESP_LOGE(TAG, "Telemetry JSON truncated");
        return;
    }

    esp_mqtt_client_publish(s_client, s_telemetry_topic, payload, length, 0, 0);
}
