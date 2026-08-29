#include "wifi_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START))
    {
        esp_wifi_connect();
    }
    else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED))
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Disconnected, reconnecting...");
        esp_wifi_connect();
    }
    else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP))
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void WiFiManager_Init(const char *ssid, const char *password)
{
    esp_err_t ret = nvs_flash_init();

    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT,
                                   ESP_EVENT_ANY_ID,
                                   &wifi_event_handler,
                                   NULL)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT,
                                   IP_EVENT_STA_GOT_IP,
                                   &wifi_event_handler,
                                   NULL)
    );

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid,
            ssid,
            sizeof(wifi_config.sta.ssid) - 1U);

    strncpy((char *)wifi_config.sta.password,
            password,
            sizeof(wifi_config.sta.password) - 1U);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool WiFiManager_IsConnected(void)
{
    if (s_wifi_event_group == NULL)
    {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);

    return (bits & WIFI_CONNECTED_BIT) != 0U;
}
