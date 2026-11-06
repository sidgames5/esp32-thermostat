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
#include "Configuration.h"
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

static bool valid_setpoints(float lower, float upper)
{
    return isfinite(lower) && isfinite(upper) &&
           lower >= THERMOSTAT_MIN_SETPOINT_F &&
           upper <= THERMOSTAT_MAX_SETPOINT_F && lower <= upper;
}

static bool adjust_setpoint(float *setpoint, int direction)
{
    float next = *setpoint + direction * 0.5f;
    if (next < THERMOSTAT_MIN_SETPOINT_F ||
        next > THERMOSTAT_MAX_SETPOINT_F) {
        return false;
    }
    *setpoint = next;
    return true;
}

static void joystick_task(void *arg)
{
    (void)arg;
    TickType_t last_adc_failure_log = 0;
    bool adc_failure_reported = false;
    adc_oneshot_unit_handle_t adc_handle;
    if (joystick_initialize(&adc_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Joystick task stopped because ADC initialization failed");
        vTaskDelete(NULL);
        return;
    }
    if (joystick_button_initialize(PIN_JOYSTICK_BUTTON) != ESP_OK) {
        ESP_LOGE(TAG, "Joystick task stopped because button initialization failed");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        app_event_t event = {.type = APP_EVENT_JOYSTICK};
        esp_err_t x_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_3, &event.joystick.x_raw);
        esp_err_t y_result = adc_oneshot_read(
            adc_handle, ADC_CHANNEL_0, &event.joystick.y_raw);
        event.joystick.button_pressed =
            joystick_button_is_pressed(PIN_JOYSTICK_BUTTON);

        if (x_result != ESP_OK || y_result != ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            if (!adc_failure_reported ||
                now - last_adc_failure_log >= pdMS_TO_TICKS(30000)) {
                ESP_LOGW(TAG, "Joystick read failed: X=%s Y=%s; retrying",
                         esp_err_to_name(x_result), esp_err_to_name(y_result));
                adc_failure_reported = true;
                last_adc_failure_log = now;
            }
        } else if (app_post_event(&event) != ESP_OK) {
            ESP_LOGW(TAG, "Dropping joystick event because event queue is full");
        } else {
            adc_failure_reported = false;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t next_read = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    TickType_t last_failure_log = 0;
    bool sensor_failure_reported = false;

    for (;;) {
        vTaskDelayUntil(&next_read, pdMS_TO_TICKS(2000));

        app_event_t event = {.type = APP_EVENT_SENSOR_READING};
        esp_err_t result = dht11_read(&event.sensor_reading);
        if (result != ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            if (!sensor_failure_reported ||
                now - last_failure_log >= pdMS_TO_TICKS(30000)) {
                ESP_LOGW(TAG, "DHT11 read failed: %s; retrying",
                         esp_err_to_name(result));
                sensor_failure_reported = true;
                last_failure_log = now;
            }
            continue;
        }

        if (sensor_failure_reported) {
            ESP_LOGI(TAG, "DHT11 reading recovered");
            sensor_failure_reported = false;
        }
        if (app_post_event(&event) != ESP_OK) {
            ESP_LOGW(TAG, "Dropping sensor event because event queue is full");
        }
    }
}

static void apply_joystick_event(const app_event_t *event,
                                 TickType_t now,
                                 thermostat_mode_t *mode,
                                 float *lower_setpoint,
                                 float *upper_setpoint,
                                 bool *editing_upper_setpoint,
                                 int *last_horizontal_direction,
                                 int *last_vertical_direction,
                                 bool *last_button_pressed,
                                 TickType_t *last_control_action,
                                 bool have_reading,
                                 const dht11_reading_t *last_reading,
                                 bool show_ip,
                                 thermostat_relay_t *heating_relay,
                                 thermostat_relay_t *cooling_relay,
                                 thermostat_relay_t *fan_relay)
{
    static const TickType_t control_cooldown = pdMS_TO_TICKS(300);
    int horizontal_direction = joystick_direction(
        joystick_normalize(event->joystick.x_raw));
    int vertical_direction = joystick_direction(
        joystick_normalize(event->joystick.y_raw));
    bool button_clicked =
        event->joystick.button_pressed && !*last_button_pressed;
    bool new_horizontal_direction =
        horizontal_direction != 0 &&
        horizontal_direction != *last_horizontal_direction;
    bool new_vertical_direction =
        vertical_direction != 0 &&
        vertical_direction != *last_vertical_direction;

    if (now - *last_control_action >= control_cooldown &&
        (button_clicked || new_horizontal_direction || new_vertical_direction)) {
        bool setpoint_changed = false;
        if (button_clicked) {
            *editing_upper_setpoint = !*editing_upper_setpoint;
        }
        if (new_vertical_direction) {
            float *selected_setpoint = *editing_upper_setpoint
                                            ? upper_setpoint
                                            : lower_setpoint;
            setpoint_changed = adjust_setpoint(selected_setpoint,
                                               vertical_direction);
        }
        if (new_horizontal_direction) {
            int next_mode = (int)*mode + horizontal_direction;
            if (next_mode < THERMOSTAT_OFF) {
                next_mode = THERMOSTAT_FAN;
            } else if (next_mode > THERMOSTAT_FAN) {
                next_mode = THERMOSTAT_OFF;
            }
            *mode = (thermostat_mode_t)next_mode;
            mqtt_publish_mode(*mode);
        }
        *last_control_action = now;

        if (setpoint_changed) {
            mqtt_publish_target_temperature(*lower_setpoint, *upper_setpoint);
        }
        if (have_reading) {
            thermostat_set_relays(*mode, last_reading->temperature,
                                  *lower_setpoint, *upper_setpoint,
                                  now, heating_relay, cooling_relay,
                                  fan_relay);
            thermostat_show_reading(last_reading, *lower_setpoint,
                                    *upper_setpoint, *editing_upper_setpoint,
                                    *mode, heating_relay->active,
                                    cooling_relay->active, show_ip);
        }
    }

    *last_horizontal_direction = horizontal_direction;
    *last_vertical_direction = vertical_direction;
    *last_button_pressed = event->joystick.button_pressed;
}

static void thermostat_task(void *arg)
{
    (void)arg;
    thermostat_mode_t mode = THERMOSTAT_OFF;
    float lower_setpoint = 70.0f;
    float upper_setpoint = 74.0f;
    bool editing_upper_setpoint = false;
    dht11_reading_t last_reading = {0};
    bool have_reading = false;
    int last_horizontal_direction = 0;
    int last_vertical_direction = 0;
    bool last_button_pressed = false;
    TickType_t last_control_action = 0;
    TickType_t last_sensor_reading = 0;
    TickType_t last_success_log = 0;
    TickType_t next_display_switch =
        xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    bool show_ip = false;
    bool sensor_timeout_reported = false;
    thermostat_relay_t heating_relay = {0};
    thermostat_relay_t cooling_relay = {0};
    thermostat_relay_t fan_relay = {0};

    if (DEBUG_BYPASS_SENSOR_VALIDATION || DEBUG_BYPASS_SENSOR_TIMEOUT ||
        DEBUG_BYPASS_TEMPERATURE_CUTOFF || DEBUG_BYPASS_RELAY_TIMING ||
        DEBUG_BYPASS_RELAY_RUNTIME_LIMITS) {
        ESP_LOGW(TAG, "DEBUG SAFETY BYPASSES ENABLED");
    }

    thermostat_show_loading(0);
    mqtt_publish_mode(mode);
    mqtt_publish_target_temperature(lower_setpoint, upper_setpoint);

    for (;;) {
        app_event_t event;
        bool received = xQueueReceive(
            app_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE;
        TickType_t now = xTaskGetTickCount();

        if (!DEBUG_BYPASS_SENSOR_TIMEOUT && have_reading &&
            now - last_sensor_reading >=
                pdMS_TO_TICKS(THERMOSTAT_SENSOR_TIMEOUT_MS)) {
            thermostat_disable_relays(
                &heating_relay, &cooling_relay, &fan_relay);
            if (!sensor_timeout_reported) {
                ESP_LOGE(TAG, "Sensor data stale; disabling all relays");
                sensor_timeout_reported = true;
            }
        }

        if (DEBUG_SHOW_IP && now >= next_display_switch) {
            show_ip = !show_ip;
            next_display_switch = now + pdMS_TO_TICKS(2000);
            if (have_reading) {
                thermostat_show_reading(&last_reading, lower_setpoint,
                                        upper_setpoint, editing_upper_setpoint,
                                        mode, heating_relay.active,
                                        cooling_relay.active, show_ip);
            }
        }

        if (!received) {
            continue;
        }

        if (event.type == APP_EVENT_JOYSTICK) {
            apply_joystick_event(
                &event, now, &mode, &lower_setpoint, &upper_setpoint,
                &editing_upper_setpoint,
                &last_horizontal_direction, &last_vertical_direction,
                &last_button_pressed,
                &last_control_action, have_reading, &last_reading,
                show_ip,
                &heating_relay, &cooling_relay, &fan_relay);
        } else if (event.type == APP_EVENT_SENSOR_READING) {
            if (!DEBUG_BYPASS_SENSOR_VALIDATION &&
                (!isfinite(event.sensor_reading.temperature) ||
                !isfinite(event.sensor_reading.humidity) ||
                event.sensor_reading.temperature <
                    THERMOSTAT_MIN_VALID_TEMP_C ||
                event.sensor_reading.temperature >
                    THERMOSTAT_MAX_VALID_TEMP_C ||
                event.sensor_reading.humidity < 0.0f ||
                event.sensor_reading.humidity > 100.0f)) {
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
            thermostat_set_relays(mode, last_reading.temperature,
                                  lower_setpoint, upper_setpoint, now,
                                  &heating_relay, &cooling_relay, &fan_relay);
            thermostat_show_reading(&last_reading, lower_setpoint,
                                    upper_setpoint, editing_upper_setpoint,
                                    mode, heating_relay.active,
                                    cooling_relay.active, show_ip);
            if (now - last_success_log >= pdMS_TO_TICKS(30000)) {
                ESP_LOGI(TAG, "DHT11 temperature: %.1f C, humidity: %.1f%%",
                         last_reading.temperature, last_reading.humidity);
                last_success_log = now;
            }
        } else if (event.type == APP_EVENT_SET_MODE) {
            if (event.mode >= THERMOSTAT_OFF &&
                event.mode <= THERMOSTAT_FAN) {
                mode = event.mode;
                if (have_reading) {
                    thermostat_set_relays(
                        mode, last_reading.temperature, lower_setpoint,
                        upper_setpoint, now,
                        &heating_relay, &cooling_relay, &fan_relay);
                    thermostat_show_reading(
                        &last_reading, lower_setpoint, upper_setpoint,
                        editing_upper_setpoint, mode, heating_relay.active,
                        cooling_relay.active, show_ip);
                }
                mqtt_publish_mode(mode);
            } else {
                ESP_LOGW(TAG, "Ignoring invalid thermostat mode: %d",
                         event.mode);
            }
        } else if (event.type == APP_EVENT_SET_SETPOINT) {
            float requested_lower = event.setpoint.lower;
            float requested_upper = event.setpoint.upper;
            if (isfinite(requested_lower) && isfinite(requested_upper)) {
                float current_mean = (lower_setpoint + upper_setpoint) / 2.0f;
                float requested_mean = (requested_lower + requested_upper) / 2.0f;
                float shift = requested_mean - current_mean;
                requested_lower = lower_setpoint + shift;
                requested_upper = upper_setpoint + shift;
            } else if (isfinite(requested_lower)) {
                requested_upper = upper_setpoint;
            } else if (isfinite(requested_upper)) {
                requested_lower = lower_setpoint;
            }
            if (valid_setpoints(requested_lower, requested_upper)) {
                lower_setpoint = requested_lower;
                upper_setpoint = requested_upper;
                if (have_reading) {
                    thermostat_set_relays(
                        mode, last_reading.temperature, lower_setpoint,
                        upper_setpoint, now,
                        &heating_relay, &cooling_relay, &fan_relay);
                    thermostat_show_reading(
                        &last_reading, lower_setpoint, upper_setpoint,
                        editing_upper_setpoint, mode, heating_relay.active,
                        cooling_relay.active, show_ip);
                }
                mqtt_publish_target_temperature(lower_setpoint, upper_setpoint);
            } else {
                ESP_LOGW(TAG, "Ignoring invalid thermostat bounds: %.1f to %.1f",
                         requested_lower, requested_upper);
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
