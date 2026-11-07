#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

void WiFiManager_Init(const char *ssid, const char *password);
bool WiFiManager_IsConnected(void);

/* Blocks until an IP address is obtained or `timeout_ms` expires. */
bool WiFiManager_WaitConnected(uint32_t timeout_ms);

#endif
