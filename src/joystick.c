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
