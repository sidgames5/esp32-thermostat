#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t wifi_initialize(void);
bool wifi_get_ip_address(char *buffer, size_t buffer_size);
