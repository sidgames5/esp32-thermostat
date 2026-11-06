#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "dht11.h"

typedef enum {
    THERMOSTAT_OFF,
    THERMOSTAT_AUTO,
    THERMOSTAT_FAN,
} thermostat_mode_t;

typedef struct {
    bool active;
    TickType_t last_transition;
    TickType_t on_since;
    TickType_t window_started;
    TickType_t on_time_in_window;
    TickType_t last_accounting;
    bool usage_initialized;
    bool transition_initialized;
    bool has_been_active;
} thermostat_relay_t;

const char *thermostat_mode_name(thermostat_mode_t mode);
void thermostat_set_relays(thermostat_mode_t mode,
                           float temperature_celsius,
                           float lower_setpoint,
                           float upper_setpoint,
                           TickType_t now,
                           thermostat_relay_t *heating_relay,
                           thermostat_relay_t *cooling_relay,
                           thermostat_relay_t *fan_relay);
void thermostat_disable_relays(thermostat_relay_t *heating_relay,
                               thermostat_relay_t *cooling_relay,
                               thermostat_relay_t *fan_relay);
void thermostat_show_reading(const dht11_reading_t *reading,
                             float lower_setpoint,
                             float upper_setpoint,
                             bool editing_upper_setpoint,
                             thermostat_mode_t mode,
                             bool show_ip);
void thermostat_show_loading(uint8_t frame);
