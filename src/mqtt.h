#pragma once

#include "esp_err.h"
#include "dht11.h"
#include "thermostat.h"

esp_err_t mqtt_initialize(void);
void mqtt_publish_temperature(const dht11_reading_t *reading);
void mqtt_publish_target_temperature(float setpoint);
void mqtt_publish_mode(thermostat_mode_t mode);
