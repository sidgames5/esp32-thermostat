#include "mqtt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "Configuration.h"
#include "app_events.h"

static const char *const TAG = "mqtt";
#ifdef ENABLE_MQTT
#include "mqtt_client.h"

static esp_mqtt_client_handle_t client;
static bool connected;
static bool target_temperature_available;
static float last_target_temperature;
static bool mode_available;
static thermostat_mode_t last_mode;

#ifdef ENABLE_MQTT
static void publish(const char *suffix, const char *payload);
#endif

static const char *ha_mode_name(thermostat_mode_t mode)
{
    switch (mode) {
    case THERMOSTAT_OFF:
        return "off";
    case THERMOSTAT_HEAT:
        return "heat";
    case THERMOSTAT_COOL:
        return "cool";
    case THERMOSTAT_AUTO:
        return "heat_cool";
    case THERMOSTAT_FAN:
        return "fan_only";
    default:
        return "off";
    }
}

static bool topic_is(const esp_mqtt_event_t *event, const char *topic)
{
    return event->topic_len == (int)strlen(topic) &&
           memcmp(event->topic, topic, event->topic_len) == 0;
}

static bool parse_float(const esp_mqtt_event_t *event, float *value)
{
    char payload[32];
    if (event->data_len <= 0 || event->data_len >= (int)sizeof(payload)) {
        return false;
    }

    memcpy(payload, event->data, event->data_len);
    payload[event->data_len] = '\0';
    char *end = NULL;
    errno = 0;
    float parsed = strtof(payload, &end);
    if (errno == ERANGE || end == payload || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static void handle_command(const esp_mqtt_event_t *event)
{
    char target_topic[96];
    char mode_topic[96];
    snprintf(target_topic, sizeof(target_topic), "%s/temperature/target/set",
             MQTT_BASE_TOPIC);
    snprintf(mode_topic, sizeof(mode_topic), "%s/mode/set", MQTT_BASE_TOPIC);

    app_event_t command = {0};
    if (topic_is(event, target_topic)) {
        command.type = APP_EVENT_SET_SETPOINT;
        if (!parse_float(event, &command.setpoint)) {
            ESP_LOGW(TAG, "Ignoring invalid target temperature payload");
            return;
        }
    } else if (topic_is(event, mode_topic)) {
        char mode[16];
        if (event->data_len <= 0 || event->data_len >= (int)sizeof(mode)) {
            ESP_LOGW(TAG, "Ignoring invalid mode payload");
            return;
        }
        memcpy(mode, event->data, event->data_len);
        mode[event->data_len] = '\0';
        command.type = APP_EVENT_SET_MODE;
        if (strcmp(mode, "OFF") == 0 || strcmp(mode, "off") == 0) {
            command.mode = THERMOSTAT_OFF;
        } else if (strcmp(mode, "HEAT") == 0 || strcmp(mode, "heat") == 0) {
            command.mode = THERMOSTAT_HEAT;
        } else if (strcmp(mode, "COOL") == 0 || strcmp(mode, "cool") == 0) {
            command.mode = THERMOSTAT_COOL;
        } else if (strcmp(mode, "AUTO") == 0 ||
                   strcmp(mode, "heat_cool") == 0) {
            command.mode = THERMOSTAT_AUTO;
        } else if (strcmp(mode, "FAN") == 0 ||
                   strcmp(mode, "fan_only") == 0) {
            command.mode = THERMOSTAT_FAN;
        } else {
            ESP_LOGW(TAG, "Ignoring invalid mode payload");
            return;
        }
    } else {
        return;
    }

    if (app_post_event(&command) != ESP_OK) {
        ESP_LOGW(TAG, "Dropping MQTT command because event queue is full");
    }
}

static void publish_topic(const char *topic, const char *payload)
{
    if (!connected) {
        return;
    }
    int message_id = esp_mqtt_client_publish(
        client, topic, payload, 0, 1, 1);
    if (message_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT topic %s", topic);
    }
}

static void publish_discovery(void)
{
    char topic[128];
    char payload[1024];
    snprintf(topic, sizeof(topic), "%s/climate/%s/config",
             MQTT_DISCOVERY_PREFIX, MQTT_CLIENT_ID);
    snprintf(payload, sizeof(payload),
             "{\"name\":\"Thermostat\","
             "\"unique_id\":\"%s_climate\","
             "\"device\":{\"identifiers\":[\"%s\"],"
             "\"name\":\"ESP32 Thermostat\","
             "\"manufacturer\":\"ESP32\","
             "\"model\":\"DIY Thermostat\"},"
             "\"availability_topic\":\"%s/availability\","
             "\"mode_state_topic\":\"%s/mode\","
             "\"mode_command_topic\":\"%s/mode/set\","
             "\"temperature_state_topic\":\"%s/temperature/target\","
             "\"temperature_command_topic\":\"%s/temperature/target/set\","
             "\"current_temperature_topic\":\"%s/temperature/current\","
             "\"modes\":[\"off\",\"heat\",\"cool\",\"heat_cool\",\"fan_only\"],"
             "\"min_temp\":50,\"max_temp\":95,\"temp_step\":0.5,"
             "\"temperature_unit\":\"F\",\"payload_available\":\"online\","
             "\"payload_not_available\":\"offline\","
             "\"qos\":1,\"retain\":true}",
             MQTT_CLIENT_ID, MQTT_CLIENT_ID, MQTT_BASE_TOPIC,
             MQTT_BASE_TOPIC, MQTT_BASE_TOPIC, MQTT_BASE_TOPIC,
             MQTT_BASE_TOPIC, MQTT_BASE_TOPIC);
    publish_topic(topic, payload);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        connected = true;
        char target_topic[96];
        char mode_topic[96];
        snprintf(target_topic, sizeof(target_topic),
                 "%s/temperature/target/set", MQTT_BASE_TOPIC);
        snprintf(mode_topic, sizeof(mode_topic), "%s/mode/set", MQTT_BASE_TOPIC);
        esp_mqtt_client_subscribe(client, target_topic, 1);
        esp_mqtt_client_subscribe(client, mode_topic, 1);
        publish_topic(MQTT_BASE_TOPIC "/availability", "online");
        publish_discovery();
        if (target_temperature_available) {
            char payload[16];
            snprintf(payload, sizeof(payload), "%.1f",
                     last_target_temperature);
            publish("temperature/target", payload);
        }
        if (mode_available) {
            publish("mode", ha_mode_name(last_mode));
        }
        ESP_LOGI(TAG, "Connected to MQTT broker");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        connected = false;
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
    } else if (event_id == MQTT_EVENT_DATA) {
        handle_command(event);
    }
}
#endif

esp_err_t mqtt_initialize(void)
{
#ifndef ENABLE_MQTT
    ESP_LOGI(TAG, "MQTT is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI,
        .session.last_will = {
            .topic = MQTT_BASE_TOPIC "/availability",
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = true,
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
            .username = MQTT_USERNAME,
            .authentication.password = MQTT_PASSWORD,
        },
    };

    client = esp_mqtt_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = esp_mqtt_client_register_event(
        client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s",
                 esp_err_to_name(result));
        return result;
    }
    result = esp_mqtt_client_start(client);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s",
                 esp_err_to_name(result));
    }
    return result;
#endif
}

#ifdef ENABLE_MQTT
static void publish(const char *suffix, const char *payload)
{
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", MQTT_BASE_TOPIC, suffix);
    publish_topic(topic, payload);
}
#endif

void mqtt_publish_temperature(const dht11_reading_t *reading)
{
#ifdef ENABLE_MQTT
    if (reading == NULL) {
        return;
    }
    char payload[16];
    snprintf(payload, sizeof(payload), "%.1f",
             reading->temperature * 9.0f / 5.0f + 32.0f);
    publish("temperature/current", payload);
#else
    (void)reading;
#endif
}

void mqtt_publish_target_temperature(float setpoint)
{
#ifdef ENABLE_MQTT
    last_target_temperature = setpoint;
    target_temperature_available = true;
    char payload[16];
    snprintf(payload, sizeof(payload), "%.1f", setpoint);
    publish("temperature/target", payload);
#else
    (void)setpoint;
#endif
}

void mqtt_publish_mode(thermostat_mode_t mode)
{
#ifdef ENABLE_MQTT
    last_mode = mode;
    mode_available = true;
    publish("mode", ha_mode_name(mode));
#else
    (void)mode;
#endif
}
