#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "app_events.h"
#include "dht11.h"
#include "hardware.h"
#include "joystick.h"
#include "lcd.h"
#include "mqtt.h"
#include "thermostat.h"
#include "hardware_config.h"
#include "wifi.h"

static const char *const TAG = "thermostat";
static QueueHandle_t app_event_queue;

esp_err_t app_post_event(const app_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(app_event_queue, event, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

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
    if (setpoint < THERMOSTAT_MIN_SETPOINT_F) {
        return THERMOSTAT_MIN_SETPOINT_F;
    }
    if (setpoint > THERMOSTAT_MAX_SETPOINT_F) {
        return THERMOSTAT_MAX_SETPOINT_F;
    }
    return setpoint;
}

static void joystick_task(void *arg)
{
    (void)arg;
    adc_oneshot_unit_handle_t adc_handle;
    if (joystick_initialize(&adc_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Joystick task stopped because ADC initialization failed");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        app_event_t event = {.type = APP_EVENT_JOYSTICK};
        esp_err_t x_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_3, &event.joystick.x_raw);
        esp_err_t y_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_0, &event.joystick.y_raw);

        if (x_result != ESP_OK || y_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read joystick: X=%s Y=%s",
                     esp_err_to_name(x_result), esp_err_to_name(y_result));
        } else if (app_post_event(&event) != ESP_OK) {
            ESP_LOGW(TAG, "Dropping joystick event because event queue is full");
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t next_read = xTaskGetTickCount() + pdMS_TO_TICKS(2000);

    for (;;) {
        vTaskDelayUntil(&next_read, pdMS_TO_TICKS(2000));

        app_event_t event = {.type = APP_EVENT_SENSOR_READING};
        esp_err_t result = dht11_read(&event.sensor_reading);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read DHT11: %s", esp_err_to_name(result));
            continue;
        }

        if (app_post_event(&event) != ESP_OK) {
            ESP_LOGW(TAG, "Dropping sensor event because event queue is full");
        }
    }
}

static void apply_joystick_event(const app_event_t *event,
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
        joystick_normalize(event->joystick.x_raw));
    int vertical_direction = joystick_direction(
        joystick_normalize(event->joystick.y_raw));
    bool new_horizontal_direction =
        horizontal_direction != 0 &&
        horizontal_direction != *last_horizontal_direction;
    bool new_vertical_direction =
        vertical_direction != 0 &&
        vertical_direction != *last_vertical_direction;

    if (now - *last_control_action >= control_cooldown &&
        (new_horizontal_direction || new_vertical_direction)) {
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

static void thermostat_task(void *arg)
{
    (void)arg;
    thermostat_mode_t mode = THERMOSTAT_OFF;
    float setpoint = 72.0f;
    dht11_reading_t last_reading = {0};
    bool have_reading = false;
    int last_horizontal_direction = 0;
    int last_vertical_direction = 0;
    TickType_t last_control_action = 0;
    TickType_t last_loading_update = 0;
    TickType_t last_sensor_reading = 0;
    uint8_t loading_frame = 0;
    bool sensor_timeout_reported = false;
    thermostat_relay_t heating_relay = {0};
    thermostat_relay_t cooling_relay = {0};
    thermostat_relay_t fan_relay = {0};

    thermostat_show_loading(0);

    for (;;) {
        app_event_t event;
        bool received = xQueueReceive(
            app_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE;
        TickType_t now = xTaskGetTickCount();

        if (have_reading &&
            now - last_sensor_reading >=
                pdMS_TO_TICKS(THERMOSTAT_SENSOR_TIMEOUT_MS)) {
            thermostat_disable_relays(
                &heating_relay, &cooling_relay, &fan_relay);
            if (!sensor_timeout_reported) {
                ESP_LOGE(TAG, "Sensor data stale; disabling all relays");
                sensor_timeout_reported = true;
            }
        }

        if (!have_reading &&
            now - last_loading_update >= pdMS_TO_TICKS(500)) {
            thermostat_show_loading(++loading_frame);
            last_loading_update = now;
        }

        if (!received) {
            continue;
        }

        if (event.type == APP_EVENT_JOYSTICK) {
            apply_joystick_event(
                &event, now, &mode, &setpoint,
                &last_horizontal_direction, &last_vertical_direction,
                &last_control_action, have_reading, &last_reading,
                &heating_relay, &cooling_relay, &fan_relay);
        } else if (event.type == APP_EVENT_SENSOR_READING) {
            if (!isfinite(event.sensor_reading.temperature) ||
                !isfinite(event.sensor_reading.humidity) ||
                event.sensor_reading.temperature <
                    THERMOSTAT_MIN_VALID_TEMP_C ||
                event.sensor_reading.temperature >
                    THERMOSTAT_MAX_VALID_TEMP_C ||
                event.sensor_reading.humidity < 0.0f ||
                event.sensor_reading.humidity > 100.0f) {
                ESP_LOGE(TAG, "Ignoring invalid sensor reading");
                thermostat_disable_relays(
                    &heating_relay, &cooling_relay, &fan_relay);
                continue;
            }
            last_reading = event.sensor_reading;
            have_reading = true;
            last_sensor_reading = now;
            sensor_timeout_reported = false;
            mqtt_publish_temperature(&last_reading);
            thermostat_set_relays(mode, last_reading.temperature, setpoint, now,
                                  &heating_relay, &cooling_relay, &fan_relay);
            thermostat_show_reading(&last_reading, setpoint, mode);
            ESP_LOGI(TAG, "DHT11 temperature: %.1f C, humidity: %.1f%%",
                     last_reading.temperature, last_reading.humidity);
        } else if (event.type == APP_EVENT_SET_MODE) {
            if (event.mode >= THERMOSTAT_OFF &&
                event.mode <= THERMOSTAT_FAN) {
                mode = event.mode;
                if (have_reading) {
                    thermostat_set_relays(
                        mode, last_reading.temperature, setpoint, now,
                        &heating_relay, &cooling_relay, &fan_relay);
                    thermostat_show_reading(&last_reading, setpoint, mode);
                }
                mqtt_publish_mode(mode);
            } else {
                ESP_LOGW(TAG, "Ignoring invalid thermostat mode: %d",
                         event.mode);
            }
        } else if (event.type == APP_EVENT_SET_SETPOINT) {
            if (isfinite(event.setpoint) &&
                event.setpoint >= THERMOSTAT_MIN_SETPOINT_F &&
                event.setpoint <= THERMOSTAT_MAX_SETPOINT_F) {
                setpoint = event.setpoint;
                if (have_reading) {
                    thermostat_set_relays(
                        mode, last_reading.temperature, setpoint, now,
                        &heating_relay, &cooling_relay, &fan_relay);
                    thermostat_show_reading(&last_reading, setpoint, mode);
                }
                mqtt_publish_target_temperature(setpoint);
            } else {
                ESP_LOGW(TAG, "Ignoring invalid thermostat setpoint: %.1f",
                         event.setpoint);
            }
        } else {
            ESP_LOGE(TAG, "Illegal application event type: %d; disabling relays",
                     event.type);
            thermostat_disable_relays(
                &heating_relay, &cooling_relay, &fan_relay);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting thermostat tasks");
    if (wifi_initialize() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed");
    }
    hardware_initialize_gpio();
    lcd_initialize();

    app_event_queue = xQueueCreate(8, sizeof(app_event_t));
    if (app_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create application event queue");
        lcd_show("Queue init failed", "");
        return;
    }

    if (mqtt_initialize() != ESP_OK) {
        ESP_LOGW(TAG, "MQTT initialization failed; continuing without MQTT");
    }

    if (xTaskCreate(joystick_task, "joystick", 3072, NULL, 5, NULL) !=
            pdPASS ||
        xTaskCreate(sensor_task, "sensor", 3072, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(thermostat_task, "thermostat", 4096, NULL, 5, NULL) !=
            pdPASS) {
        ESP_LOGE(TAG, "Failed to create one or more application tasks");
        lcd_show("Task init failed", "");
    }
}
