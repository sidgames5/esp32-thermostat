#include "thermostat.h"

#include "driver/gpio.h"
#include "lcd.h"
#include "hardware_config.h"

static const TickType_t compressor_minimum_time =
    pdMS_TO_TICKS(5 * 60 * 1000);
static const TickType_t fan_minimum_time = pdMS_TO_TICKS(3 * 60 * 1000);

const char *thermostat_mode_name(thermostat_mode_t mode)
{
    static const char *const names[] = {"OFF", "HEAT", "COOL", "AUTO", "FAN"};
    return names[mode];
}

static void update_relay(int pin,
                         bool requested,
                         TickType_t minimum_time,
                         TickType_t now,
                         thermostat_relay_t *relay)
{
    if (requested == relay->active ||
        now - relay->last_transition < minimum_time) {
        return;
    }

    relay->active = requested;
    relay->last_transition = now;
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
    float temperature = temperature_celsius * 9.0f / 5.0f + 32.0f;
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
