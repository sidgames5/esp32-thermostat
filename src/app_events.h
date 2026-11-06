#pragma once

#include "esp_err.h"
#include "thermostat.h"

typedef enum {
    APP_EVENT_JOYSTICK,
    APP_EVENT_SENSOR_READING,
    APP_EVENT_SET_MODE,
    APP_EVENT_SET_SETPOINT,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        struct {
            int x_raw;
            int y_raw;
        } joystick;
        dht11_reading_t sensor_reading;
        thermostat_mode_t mode;
        float setpoint;
    };
} app_event_t;

esp_err_t app_post_event(const app_event_t *event);
