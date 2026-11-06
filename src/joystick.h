#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t joystick_initialize(adc_oneshot_unit_handle_t *adc_handle);
float joystick_normalize(int raw_value);
int joystick_direction(float value);
