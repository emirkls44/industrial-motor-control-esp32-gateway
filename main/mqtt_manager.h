#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*MQTTCommandCallback_t)(const char *command);

void MQTTManager_SetCommandCallback(MQTTCommandCallback_t callback);
void MQTTManager_Init(const char *broker_uri);

bool MQTTManager_IsConnected(void);

void MQTTManager_PublishTelemetry(uint8_t state,
                                  uint8_t command_duty,
                                  uint8_t target_duty,
                                  uint8_t current_duty,
                                  uint8_t estop,
                                  uint16_t adc_raw);

#endif
