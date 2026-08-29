#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

void WiFiManager_Init(const char *ssid, const char *password);
bool WiFiManager_IsConnected(void);

#endif
