#include "joystick.h"

#include "esp_log.h"

static const char *const TAG = "joystick";

esp_err_t joystick_initialize(adc_oneshot_unit_handle_t *adc_handle)
{
    adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = ADC_UNIT_1};
    esp_err_t result = adc_oneshot_new_unit(&unit_config, adc_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize joystick ADC: %s",
                 esp_err_to_name(result));
        return result;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_oneshot_config_channel(
        *adc_handle, ADC_CHANNEL_3, &channel_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure joystick X ADC: %s",
                 esp_err_to_name(result));
        return result;
    }

    result = adc_oneshot_config_channel(
        *adc_handle, ADC_CHANNEL_0, &channel_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure joystick Y ADC: %s",
                 esp_err_to_name(result));
    }
    return result;
}

esp_err_t joystick_button_initialize(gpio_num_t button_pin)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << button_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&button_config);
}

bool joystick_button_is_pressed(gpio_num_t button_pin)
{
    return gpio_get_level(button_pin) == 0;
}

float joystick_normalize(int raw_value)
{
    return ((raw_value / 4095.0f) * 2.0f - 1.0f) * -1.0f;
}

int joystick_direction(float value)
{
    if (value < -0.5f) {
        return -1;
    }
    if (value > 0.5f) {
        return 1;
    }
    return 0;
}
