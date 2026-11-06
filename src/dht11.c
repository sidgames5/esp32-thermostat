#include "dht11.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "hardware_config.h"

static bool wait_for_level(int level, uint32_t timeout_us)
{
    while (timeout_us-- > 0) {
        if (gpio_get_level(PIN_DHT11) == level) {
            return true;
        }
        esp_rom_delay_us(1);
    }
    return false;
}

esp_err_t dht11_read(dht11_reading_t *reading)
{
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_direction(PIN_DHT11, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_DHT11, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_DHT11, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(PIN_DHT11, GPIO_MODE_INPUT);

    if (!wait_for_level(0, 100) ||
        !wait_for_level(1, 100) ||
        !wait_for_level(0, 100)) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t data[5] = {0};
    for (int bit = 0; bit < 40; bit++) {
        if (!wait_for_level(1, 100)) {
            return ESP_ERR_TIMEOUT;
        }

        uint32_t high_time_us = 0;
        while (gpio_get_level(PIN_DHT11) == 1 && high_time_us < 100) {
            esp_rom_delay_us(1);
            high_time_us++;
        }
        if (high_time_us >= 100) {
            return ESP_ERR_TIMEOUT;
        }

        data[bit / 8] <<= 1;
        if (high_time_us > 40) {
            data[bit / 8] |= 1;
        }
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    reading->humidity = data[0] + data[1] / 10.0f;
    reading->temperature = data[2] + data[3] / 10.0f;
    return ESP_OK;
}
