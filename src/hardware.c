#include "hardware.h"

#include "driver/gpio.h"

#include "hardware_config.h"

void hardware_initialize_gpio(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED) |
                        (1ULL << PIN_DISPLAY_RS) |
                        (1ULL << PIN_DISPLAY_E) |
                        (1ULL << PIN_DISPLAY_D4) |
                        (1ULL << PIN_DISPLAY_D5) |
                        (1ULL << PIN_DISPLAY_D6) |
                        (1ULL << PIN_DISPLAY_D7) |
                        (1ULL << PIN_RELAY_HEAT) |
                        (1ULL << PIN_RELAY_COOL) |
                        (1ULL << PIN_RELAY_FAN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&output_config);

    gpio_set_level(PIN_STATUS_LED, 1);
    gpio_set_level(PIN_DISPLAY_RS, 0);
    gpio_set_level(PIN_DISPLAY_E, 0);
    gpio_set_level(PIN_DISPLAY_D4, 0);
    gpio_set_level(PIN_DISPLAY_D5, 0);
    gpio_set_level(PIN_DISPLAY_D6, 0);
    gpio_set_level(PIN_DISPLAY_D7, 0);
    gpio_set_level(PIN_RELAY_HEAT, 0);
    gpio_set_level(PIN_RELAY_COOL, 0);
    gpio_set_level(PIN_RELAY_FAN, 0);

    gpio_config_t joystick_config = {
        .pin_bit_mask = (1ULL << PIN_JOYSTICK_X) |
                        (1ULL << PIN_JOYSTICK_Y),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&joystick_config);

    gpio_config_t dht11_config = {
        .pin_bit_mask = (1ULL << PIN_DHT11),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dht11_config);
}
