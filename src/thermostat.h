#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "dht11.h"

typedef enum {
    THERMOSTAT_OFF,
    THERMOSTAT_HEAT,
    THERMOSTAT_COOL,
    THERMOSTAT_AUTO,
    THERMOSTAT_FAN,
} thermostat_mode_t;

typedef struct {
    bool active;
    TickType_t last_transition;
} thermostat_relay_t;

const char *thermostat_mode_name(thermostat_mode_t mode);
void thermostat_set_relays(thermostat_mode_t mode,
                           float temperature_celsius,
                           float setpoint,
                           TickType_t now,
                           thermostat_relay_t *heating_relay,
                           thermostat_relay_t *cooling_relay,
                           thermostat_relay_t *fan_relay);
void thermostat_show_reading(const dht11_reading_t *reading,
                             float setpoint,
                             thermostat_mode_t mode);
void thermostat_show_loading(uint8_t frame);
