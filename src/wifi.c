#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "Configuration.h"

static const char *const TAG = "wifi";
static EventGroupHandle_t wifi_event_group;
static esp_event_handler_instance_t wifi_event_handler_instance;
static esp_event_handler_instance_t ip_event_handler_instance;
static esp_netif_t *wifi_netif;

enum {
    WIFI_CONNECTED_BIT = BIT0,
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Wi-Fi connection: %s",
                     esp_err_to_name(result));
        }
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying");
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to retry Wi-Fi connection: %s",
                     esp_err_to_name(result));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Connected with IP address " IPSTR,
                 IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_initialize(void)
{
#ifndef ENABLE_WIFI
    ESP_LOGI(TAG, "Wi-Fi is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (WIFI_SSID[0] == '\0' || WIFI_PASSWORD[0] == '\0' ||
        (WIFI_SSID[0] == 'y' && WIFI_SSID[1] == 'o' &&
         WIFI_SSID[2] == 'u')) {
        ESP_LOGE(TAG, "Configure WIFI_SSID and WIFI_PASSWORD in Configuration.h");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result != ESP_OK) {
            return result;
        }
        result = nvs_flash_init();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(result));
        return result;
    }

    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    wifi_netif = esp_netif_create_default_wifi_sta();
    if (wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi station interface");
        return ESP_ERR_NO_MEM;
    }

    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_init_config);
    if (result != ESP_OK) {
        return result;
    }

    result = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &wifi_event_handler_instance);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
        &ip_event_handler_instance);
    if (result != ESP_OK) {
        return result;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK) {
        result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (result == ESP_OK) {
        result = esp_wifi_start();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(result));
        return result;
    }

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Timed out waiting for Wi-Fi connection");
    return ESP_ERR_TIMEOUT;
#endif
}

bool wifi_get_ip_address(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';
    if (wifi_netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(wifi_netif, &ip_info) != ESP_OK ||
        ip_info.ip.addr == 0) {
        return false;
    }

    int written = snprintf(buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
    return written >= 0 && (size_t)written < buffer_size;
}
