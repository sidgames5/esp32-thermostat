#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "dht11.h"
#include "hardware.h"
#include "joystick.h"
#include "lcd.h"
#include "thermostat.h"

static const char *const TAG = "HELLO";

static thermostat_mode_t next_mode(thermostat_mode_t mode, int direction)
{
    int value = (int)mode + direction;
    if (value < THERMOSTAT_OFF) {
        value = THERMOSTAT_FAN;
    } else if (value > THERMOSTAT_FAN) {
        value = THERMOSTAT_OFF;
    }
    return (thermostat_mode_t)value;
}

static float next_setpoint(float setpoint, int direction)
{
    setpoint += direction * 0.5f;
    if (setpoint < 50.0f) {
        return 50.0f;
    }
    if (setpoint > 95.0f) {
        return 95.0f;
    }
    return setpoint;
}

static void handle_joystick(int x_raw,
                            int y_raw,
                            TickType_t now,
                            thermostat_mode_t *mode,
                            float *setpoint,
                            int *last_horizontal_direction,
                            int *last_vertical_direction,
                            TickType_t *last_control_action,
                            bool have_reading,
                            const dht11_reading_t *last_reading,
                            thermostat_relay_t *heating_relay,
                            thermostat_relay_t *cooling_relay,
                            thermostat_relay_t *fan_relay)
{
    static const TickType_t control_cooldown = pdMS_TO_TICKS(300);
    int horizontal_direction = joystick_direction(
        joystick_normalize(x_raw));
    int vertical_direction = joystick_direction(
        joystick_normalize(y_raw));
    bool new_horizontal_direction =
        horizontal_direction != 0 &&
        horizontal_direction != *last_horizontal_direction;
    bool new_vertical_direction =
        vertical_direction != 0 &&
        vertical_direction != *last_vertical_direction;
    bool can_act = now - *last_control_action >= control_cooldown;

    if (can_act && (new_horizontal_direction || new_vertical_direction)) {
        if (new_horizontal_direction) {
            *mode = next_mode(*mode, horizontal_direction);
        }
        if (new_vertical_direction) {
            *setpoint = next_setpoint(*setpoint, vertical_direction);
        }
        *last_control_action = now;

        if (have_reading) {
            thermostat_set_relays(*mode, last_reading->temperature, *setpoint,
                                  now, heating_relay, cooling_relay,
                                  fan_relay);
            thermostat_show_reading(last_reading, *setpoint, *mode);
        }
    }

    *last_horizontal_direction = horizontal_direction;
    *last_vertical_direction = vertical_direction;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Hello NodeMCU!");
    hardware_initialize_gpio();
    lcd_initialize();
    thermostat_show_loading(0);

    adc_oneshot_unit_handle_t adc_handle;
    if (joystick_initialize(&adc_handle) != ESP_OK) {
        lcd_show("ADC init failed", "");
        return;
    }

    thermostat_mode_t mode = THERMOSTAT_OFF;
    float setpoint = 72.0f;
    dht11_reading_t last_reading = {0};
    bool have_reading = false;
    int last_horizontal_direction = 0;
    int last_vertical_direction = 0;
    TickType_t last_control_action = 0;
    TickType_t last_loading_update = 0;
    TickType_t last_dht11_read = 0;
    uint8_t loading_frame = 0;
    thermostat_relay_t heating_relay = {false, 0};
    thermostat_relay_t cooling_relay = {false, 0};
    thermostat_relay_t fan_relay = {false, 0};

    for (;;) {
        int joystick_x;
        int joystick_y;
        esp_err_t x_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_3, &joystick_x);
        esp_err_t y_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_0, &joystick_y);
        TickType_t now = xTaskGetTickCount();

        if (!have_reading &&
            now - last_loading_update >= pdMS_TO_TICKS(500)) {
            loading_frame++;
            thermostat_show_loading(loading_frame);
            last_loading_update = now;
        }

        if (x_result != ESP_OK || y_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read joystick: X=%s Y=%s",
                     esp_err_to_name(x_result), esp_err_to_name(y_result));
        } else {
            handle_joystick(
                joystick_x, joystick_y, now, &mode, &setpoint,
                &last_horizontal_direction, &last_vertical_direction,
                &last_control_action, have_reading, &last_reading,
                &heating_relay, &cooling_relay, &fan_relay);
        }

        if (now - last_dht11_read >= pdMS_TO_TICKS(2000)) {
            dht11_reading_t reading;
            esp_err_t result = dht11_read(&reading);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read DHT11: %s",
                         esp_err_to_name(result));
            } else {
                last_reading = reading;
                have_reading = true;
                thermostat_set_relays(mode, reading.temperature, setpoint, now,
                                      &heating_relay, &cooling_relay,
                                      &fan_relay);
                thermostat_show_reading(&reading, setpoint, mode);
                ESP_LOGI(TAG, "DHT11 temperature: %.1f C, humidity: %.1f%%",
                         reading.temperature, reading.humidity);
            }
            last_dht11_read = now;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
