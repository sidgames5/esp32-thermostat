#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t joystick_initialize(adc_oneshot_unit_handle_t *adc_handle);
esp_err_t joystick_button_initialize(gpio_num_t button_pin);
bool joystick_button_is_pressed(gpio_num_t button_pin);
float joystick_normalize(int raw_value);
int joystick_direction(float value);
