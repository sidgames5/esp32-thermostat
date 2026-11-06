#include "thermostat.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "Configuration.h"
#include "lcd.h"
#include "hardware_config.h"

static const char *const TAG = "thermostat";
static const TickType_t compressor_minimum_time =
    pdMS_TO_TICKS(5 * 60 * 1000);
static const TickType_t fan_minimum_time = pdMS_TO_TICKS(3 * 60 * 1000);

const char *thermostat_mode_name(thermostat_mode_t mode)
{
    static const char *const names[] = {"OFF", "HEAT", "COOL", "AUTO", "FAN"};
    if (mode < THERMOSTAT_OFF || mode > THERMOSTAT_FAN) {
        return "INVALID";
    }
    return names[mode];
}

static void account_relay_usage(thermostat_relay_t *relay, TickType_t now)
{
    if (!relay->usage_initialized) {
        relay->window_started = now;
        relay->last_accounting = now;
        relay->usage_initialized = true;
        return;
    }
    if (now - relay->window_started >=
        pdMS_TO_TICKS(THERMOSTAT_DUTY_WINDOW_MS)) {
        relay->window_started = now;
        relay->on_time_in_window = 0;
    } else if (relay->active) {
        relay->on_time_in_window += now - relay->last_accounting;
    }
    relay->last_accounting = now;
}

static void force_relay_off(int pin, thermostat_relay_t *relay, TickType_t now)
{
    relay->active = false;
    relay->last_transition = now;
    relay->on_since = 0;
    relay->transition_initialized = true;
    gpio_set_level(pin, 0);
}

static void update_relay(int pin,
                         bool requested,
                         TickType_t minimum_time,
                         TickType_t now,
                         thermostat_relay_t *relay)
{
    account_relay_usage(relay, now);
    if (!DEBUG_BYPASS_RELAY_RUNTIME_LIMITS && relay->active &&
        (now - relay->on_since >=
             pdMS_TO_TICKS(THERMOSTAT_MAX_CONTINUOUS_RUNTIME_MS) ||
         relay->on_time_in_window >= pdMS_TO_TICKS(THERMOSTAT_MAX_DUTY_MS))) {
        ESP_LOGE(TAG, "Relay safety limit reached on GPIO %d", pin);
        force_relay_off(pin, relay, now);
        requested = false;
    }
    if (!DEBUG_BYPASS_RELAY_RUNTIME_LIMITS &&
        relay->on_time_in_window >= pdMS_TO_TICKS(THERMOSTAT_MAX_DUTY_MS)) {
        requested = false;
    }

    if (requested == relay->active ||
        (!DEBUG_BYPASS_RELAY_TIMING && relay->transition_initialized &&
         now - relay->last_transition < minimum_time)) {
        return;
    }

    relay->active = requested;
    relay->last_transition = now;
    relay->on_since = requested ? now : 0;
    relay->transition_initialized = true;
    gpio_set_level(pin, relay->active);
}

void thermostat_set_relays(thermostat_mode_t mode,
                           float temperature_celsius,
                           float setpoint,
                           TickType_t now,
                           thermostat_relay_t *heating_relay,
                           thermostat_relay_t *cooling_relay,
                           thermostat_relay_t *fan_relay)
{
    const float hysteresis = 1.0f;
    if (mode < THERMOSTAT_OFF || mode > THERMOSTAT_FAN ||
        (!DEBUG_BYPASS_SENSOR_VALIDATION &&
         (!isfinite(temperature_celsius) ||
          temperature_celsius < THERMOSTAT_MIN_VALID_TEMP_C ||
          temperature_celsius > THERMOSTAT_MAX_VALID_TEMP_C)) ||
        !isfinite(setpoint) ||
        setpoint < THERMOSTAT_MIN_SETPOINT_F ||
        setpoint > THERMOSTAT_MAX_SETPOINT_F) {
        ESP_LOGE(TAG, "Invalid control state; disabling all relays");
        thermostat_disable_relays(heating_relay, cooling_relay, fan_relay);
        return;
    }

    float temperature = temperature_celsius * 9.0f / 5.0f + 32.0f;
    if (!DEBUG_BYPASS_TEMPERATURE_CUTOFF &&
        temperature >= THERMOSTAT_MAX_SAFE_TEMP_F) {
        ESP_LOGE(TAG, "Over-temperature cutoff at %.1f F", temperature);
        thermostat_disable_relays(heating_relay, cooling_relay, fan_relay);
        return;
    }
    bool heating = false;
    bool cooling = false;

    if (mode == THERMOSTAT_HEAT) {
        heating = temperature <= setpoint - hysteresis;
    } else if (mode == THERMOSTAT_COOL) {
        cooling = temperature >= setpoint + hysteresis;
    } else if (mode == THERMOSTAT_AUTO) {
        heating = temperature <= setpoint - hysteresis;
        cooling = temperature >= setpoint + hysteresis;
    }

    bool fan_requested = mode == THERMOSTAT_FAN ||
                         heating || cooling ||
                         heating_relay->active || cooling_relay->active;
    update_relay(PIN_RELAY_FAN, fan_requested, fan_minimum_time,
                 now, fan_relay);

    bool heating_requested = heating && !cooling_relay->active &&
                             fan_relay->active;
    bool cooling_requested = cooling && !heating_relay->active &&
                             fan_relay->active;
    update_relay(PIN_RELAY_HEAT, heating_requested, compressor_minimum_time,
                 now, heating_relay);
    update_relay(PIN_RELAY_COOL, cooling_requested, compressor_minimum_time,
                 now, cooling_relay);
}

void thermostat_disable_relays(thermostat_relay_t *heating_relay,
                               thermostat_relay_t *cooling_relay,
                               thermostat_relay_t *fan_relay)
{
    TickType_t now = xTaskGetTickCount();
    force_relay_off(PIN_RELAY_HEAT, heating_relay, now);
    force_relay_off(PIN_RELAY_COOL, cooling_relay, now);
    force_relay_off(PIN_RELAY_FAN, fan_relay, now);
}

void thermostat_show_reading(const dht11_reading_t *reading,
                             float setpoint,
                             thermostat_mode_t mode)
{
    float temperature = reading->temperature * 9.0f / 5.0f + 32.0f;
    lcd_printf_line(0, "Now %4.1fF", temperature);
    lcd_printf_line(1, "Set %4.1fF %-4s", setpoint,
                    thermostat_mode_name(mode));
}

void thermostat_show_loading(uint8_t frame)
{
    static const char *const loading_frames[] = {
        "Loading.        ",
        "Loading..       ",
        "Loading...      ",
    };
    lcd_write_line(0, loading_frames[frame % 3]);
    lcd_write_line(1, "Waiting for data");
}
