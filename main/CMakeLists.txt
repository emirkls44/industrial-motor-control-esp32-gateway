idf_component_register(
    SRCS
        "main.c"
        "command_parser.c"
        "mqtt_manager.c"
        "protocol.c"
        "uart_link.c"
        "wifi_manager.c"

    INCLUDE_DIRS "."

    REQUIRES
        esp_driver_uart
        esp_event
        esp_netif
        esp_wifi
        mqtt
        nvs_flash
)
