#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Topics (DEVICE_ID is set in menuconfig, default "motor01"):
 *   DEVICE_ID/telemetry  JSON telemetry, published for every STM32 frame
 *   DEVICE_ID/command    START | STOP | DUTY <0-100>   (subscribed)
 *   DEVICE_ID/status     "online" / "offline" (retained, last will)
 */

typedef struct
{
    const char *broker_uri; /* mqtt://host:1883 or mqtts://host:8883 */
    const char *username;   /* NULL or "" for no authentication     */
    const char *password;
    const char *device_id;
} MQTTManagerConfig_t;

/* Called from the MQTT task with the received command text. */
typedef void (*MQTTCommandCallback_t)(const char *command);

void MQTTManager_Init(const MQTTManagerConfig_t *config, MQTTCommandCallback_t callback);

bool MQTTManager_IsConnected(void);

void MQTTManager_PublishTelemetry(const ProtocolTelemetry_t *telemetry, uint8_t seq,
                                  uint32_t lost_frames);

#endif /* MQTT_MANAGER_H */
