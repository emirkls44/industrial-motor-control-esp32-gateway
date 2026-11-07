#ifndef UART_LINK_H
#define UART_LINK_H

#include "esp_err.h"
#include "protocol.h"

#include <stdint.h>

/* Serial link to the STM32 motor controller. */

typedef struct
{
    int port;
    int tx_pin;
    int rx_pin;
    int baudrate;
} UartLinkConfig_t;

typedef struct
{
    uint32_t telemetry_frames; /* valid telemetry frames received             */
    uint32_t lost_frames;      /* gaps detected in the telemetry sequence     */
    uint32_t crc_errors;       /* frames dropped by the parser (bad CRC)      */
    uint32_t format_errors;    /* frames dropped by the parser (bad header)   */
} UartLinkStats_t;

/* Called from the UART receive task for every valid telemetry frame. */
typedef void (*UartLinkTelemetryCallback_t)(const ProtocolTelemetry_t *telemetry, uint8_t seq);

esp_err_t UartLink_Init(const UartLinkConfig_t *config, UartLinkTelemetryCallback_t callback);

/* Thread-safe: may be called from any task. */
esp_err_t UartLink_SendCommand(ProtocolCommand_t command, uint8_t value);

UartLinkStats_t UartLink_GetStats(void);

#endif /* UART_LINK_H */
