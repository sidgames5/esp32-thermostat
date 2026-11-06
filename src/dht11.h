#pragma once

#include "esp_err.h"

typedef struct {
    float humidity;
    float temperature;
} dht11_reading_t;

esp_err_t dht11_read(dht11_reading_t *reading);
