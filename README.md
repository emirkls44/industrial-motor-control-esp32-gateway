# ESP32 MQTT Gateway

STM32 tabanlı motor kontrol sisteminden alınan telemetri verilerini UART üzerinden okuyup MQTT ile bilgisayara ileten ESP32 haberleşme katmanı.

STM32 → UART → ESP32 → Wi-Fi → MQTT Broker → PC

- UART üzerinden sabit uzunluklu veri paketi alma
- CRC-8 ile paket doğrulama
- Wi-Fi bağlantısı
- MQTT üzerinden telemetri yayını
- Telemetri verilerini JSON formatında iletme

MQTT Topic
`motor01/telemetry`
UART-MQTT telemetri hattı çalışır durumdadır.**
