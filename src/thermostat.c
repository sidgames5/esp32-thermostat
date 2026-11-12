#include "thermostat.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "Configuration.h"
#include "lcd.h"
#include "hardware_config.h"
#include "wifi.h"

static const char *const TAG = "thermostat";
static const TickType_t compressor_minimum_off_time =
    pdMS_TO_TICKS(5 * 60 * 1000);
static const TickType_t heat_cool_transition_lockout =
    pdMS_TO_TICKS(5 * 60 * 1000);
static const TickType_t furnace_minimum_off_time = pdMS_TO_TICKS(60 * 1000);
static const TickType_t fan_minimum_on_time = pdMS_TO_TICKS(60 * 1000);
static const TickType_t fan_pre_run_time = pdMS_TO_TICKS(10 * 1000);
static const TickType_t fan_post_run_time = pdMS_TO_TICKS(60 * 1000);

const char *thermostat_mode_name(thermostat_mode_t mode)
{
    static const char *const names[] = {"OFF", "AUTO", "FAN"};
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
                         TickType_t minimum_on_time,
                         TickType_t minimum_off_time,
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
        (!DEBUG_BYPASS_RELAY_TIMING && requested && relay->has_been_active &&
         relay->transition_initialized &&
         now - relay->last_transition <
             minimum_off_time) ||
        (!DEBUG_BYPASS_RELAY_TIMING && !requested &&
         relay->transition_initialized &&
         now - relay->last_transition < minimum_on_time)) {
        return;
    }

    relay->active = requested;
    if (requested) {
        relay->has_been_active = true;
    }
    relay->last_transition = now;
    relay->on_since = requested ? now : 0;
    relay->transition_initialized = true;
    gpio_set_level(pin, relay->active);
}

void thermostat_set_relays(thermostat_mode_t mode,
                           float temperature_celsius,
                           float lower_setpoint,
                           float upper_setpoint,
                           TickType_t now,
                           thermostat_relay_t *heating_relay,
                           thermostat_relay_t *cooling_relay,
                           thermostat_relay_t *fan_relay)
{
    const float hysteresis = 0.0f;
    if (mode < THERMOSTAT_OFF || mode > THERMOSTAT_FAN ||
        (!DEBUG_BYPASS_SENSOR_VALIDATION &&
         (!isfinite(temperature_celsius) ||
          temperature_celsius < THERMOSTAT_MIN_VALID_TEMP_C ||
          temperature_celsius > THERMOSTAT_MAX_VALID_TEMP_C)) ||
        !isfinite(lower_setpoint) || !isfinite(upper_setpoint) ||
        lower_setpoint < THERMOSTAT_MIN_SETPOINT_F ||
        upper_setpoint > THERMOSTAT_MAX_SETPOINT_F ||
        lower_setpoint > upper_setpoint) {
        ESP_LOGE(TAG, "Invalid control state; disabling all relays");
        thermostat_disable_relays(heating_relay, cooling_relay, fan_relay);
        return;
    }

    float temperature = temperature_celsius * 9.0f / 5.0f + 32.0f;
    if (mode == THERMOSTAT_OFF) {
        thermostat_disable_relays(heating_relay, cooling_relay, fan_relay);
        return;
    }

    if (mode == THERMOSTAT_FAN) {
        if (heating_relay->active) {
            account_relay_usage(heating_relay, now);
            force_relay_off(PIN_RELAY_HEAT, heating_relay, now);
        }
        if (cooling_relay->active) {
            account_relay_usage(cooling_relay, now);
            force_relay_off(PIN_RELAY_COOL, cooling_relay, now);
        }
    }

    bool heating = false;
    bool cooling = false;

    if (mode == THERMOSTAT_AUTO) {
        heating = temperature <= lower_setpoint - hysteresis;
        cooling = temperature >= upper_setpoint + hysteresis;
    }

    bool heat_or_cool_requested = heating || cooling;
    bool heating_transition_lockout =
        !DEBUG_BYPASS_RELAY_TIMING && !cooling_relay->active &&
        cooling_relay->has_been_active &&
        cooling_relay->transition_initialized &&
        now - cooling_relay->last_transition < heat_cool_transition_lockout;
    bool cooling_transition_lockout =
        !DEBUG_BYPASS_RELAY_TIMING && !heating_relay->active &&
        heating_relay->has_been_active &&
        heating_relay->transition_initialized &&
        now - heating_relay->last_transition < heat_cool_transition_lockout;
    bool compressor_recently_stopped =
        (!heating_relay->active && heating_relay->has_been_active &&
         heating_relay->transition_initialized &&
         now - heating_relay->last_transition < fan_post_run_time) ||
        (!cooling_relay->active && cooling_relay->has_been_active &&
         cooling_relay->transition_initialized &&
         now - cooling_relay->last_transition < fan_post_run_time);
    bool fan_requested = mode == THERMOSTAT_FAN ||
                         heat_or_cool_requested ||
                         heating_relay->active || cooling_relay->active ||
                         compressor_recently_stopped;
    update_relay(PIN_RELAY_FAN, fan_requested, fan_minimum_on_time, 0,
                 now, fan_relay);

    bool heating_requested = heating && !cooling_relay->active &&
                             fan_relay->active &&
                             !heating_transition_lockout &&
                             (DEBUG_BYPASS_RELAY_TIMING ||
                              now - fan_relay->on_since >= fan_pre_run_time);
    bool cooling_requested = cooling && !heating_relay->active &&
                             fan_relay->active &&
                             !cooling_transition_lockout &&
                             (DEBUG_BYPASS_RELAY_TIMING ||
                              now - fan_relay->on_since >= fan_pre_run_time);
    update_relay(PIN_RELAY_HEAT, heating_requested,
                 0, furnace_minimum_off_time,
                 now, heating_relay);
    update_relay(PIN_RELAY_COOL, cooling_requested,
                 0, compressor_minimum_off_time,
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

void thermostat_show_thermal_runaway(void)
{
    lcd_show("THERMAL RUNAWAY", "POWER CYCLE");
}

void thermostat_show_reading(const dht11_reading_t *reading,
                             float lower_setpoint,
                             float upper_setpoint,
                             bool editing_upper_setpoint,
                             thermostat_mode_t mode,
                             bool heating_active,
                             bool cooling_active,
                             bool show_ip)
{
    float temperature = reading->temperature * 9.0f / 5.0f + 32.0f;
    if (show_ip) {
        char ip_address[16];
        if (wifi_get_ip_address(ip_address, sizeof(ip_address))) {
            lcd_write_line(0, ip_address);
        } else {
            lcd_write_line(0, "Offline");
        }
    } else {
        const char *display_mode = thermostat_mode_name(mode);
        if (mode == THERMOSTAT_AUTO) {
            if (heating_active) {
                display_mode = "HEATING";
            } else if (cooling_active) {
                display_mode = "COOLING";
            }
        }
        lcd_printf_line(0, "T: %4.1fF %-4s", temperature,
                        display_mode);
    }
    lcd_printf_line(1, "%cL:%4.1f %cH:%4.1f",
                    editing_upper_setpoint ? ' ' : '*', lower_setpoint,
                    editing_upper_setpoint ? '*' : ' ', upper_setpoint);
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
