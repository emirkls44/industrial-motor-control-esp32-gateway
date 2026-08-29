#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "local_config.h"

#define UART_PORT       UART_NUM_2
#define UART_RX_PIN     16
#define UART_BAUDRATE   115200
#define BUFFER_SIZE     1024

static const char *TAG = "STM32_UART";  
static uint8_t crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0x00;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }

    return crc;
}
void app_main(void)
{
    
    
    

WiFiManager_Init(wifi_name, wifi_password);

while (!WiFiManager_IsConnected())
{
    vTaskDelay(pdMS_TO_TICKS(200));
}

MQTTManager_Init("mqtt://192.168.1.103:1883");

    uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

ESP_ERROR_CHECK(
    uart_driver_install(UART_PORT, BUFFER_SIZE, 0, 0, NULL, 0)
);

ESP_ERROR_CHECK(
    uart_param_config(UART_PORT, &uart_config)
);

ESP_ERROR_CHECK(
    uart_set_pin(
        UART_PORT,
        UART_PIN_NO_CHANGE,
        UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    )
);

    uint8_t data[BUFFER_SIZE];

    while (1)
    {
        int len = uart_read_bytes(
            UART_PORT,
            data,
            BUFFER_SIZE,
            pdMS_TO_TICKS(100)
        );

if (len == 14)
{
    if ((data[0] == 0xAA) && (data[1] == 0x55))
    {
        uint8_t calculated_crc = crc8(&data[2], 11);

        if (calculated_crc != data[13])
        {
            ESP_LOGE(TAG, "CRC ERROR");
            continue;
        }

        uint8_t sequence     = data[5];
        uint8_t state        = data[6];
        uint8_t command_duty = data[7];
        uint8_t target_duty  = data[8];
        uint8_t current_duty = data[9];
        uint8_t estop        = data[10];

        uint16_t adc_raw =
            ((uint16_t)data[12] << 8) | data[11];

            MQTTManager_PublishTelemetry(
    state,
    command_duty,
    target_duty,
    current_duty,
    estop,
    adc_raw
);

        ESP_LOGI(TAG,
                 "SEQ:%u STATE:%u CMD:%u TARGET:%u CURRENT:%u ESTOP:%u ADC:%u",
                 sequence,
                 state,
                 command_duty,
                 target_duty,
                 current_duty,
                 estop,
                 adc_raw);
    }
}

    }
}