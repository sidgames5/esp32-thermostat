// | ~~~~~~~~~~~~~~~~~~~~~~~ |
// |                         |
// | CONFIGURATION_EXAMPLE.H |
// |                         |
// | ~~~~~~~~~~~~~~~~~~~~~~~ |

// | ~~~~~~~~~~~~~~~~~~~~~~~ |
// |   DEBUG SAFETY BYPASSES |
// | ~~~~~~~~~~~~~~~~~~~~~~~ |
//
// These switches are for bench debugging only. Keep all of them at 0 for
// normal operation. Bypassing these protections can damage hardware or create
// unsafe temperatures.

#define DEBUG_BYPASS_SENSOR_VALIDATION 0
#define DEBUG_BYPASS_SENSOR_TIMEOUT 0
#define DEBUG_BYPASS_TEMPERATURE_CUTOFF 0
#define DEBUG_BYPASS_RELAY_TIMING 0
#define DEBUG_BYPASS_RELAY_RUNTIME_LIMITS 0
#define DEBUG_SHOW_IP 0

// | ~~~~~~~~~~~~~ |
// |     Wi-Fi     |
// | ~~~~~~~~~~~~~ |

// Properly configured Wi-Fi is mandatory for MQTT to work

#define ENABLE_WIFI

#ifdef ENABLE_WIFI

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

#endif

// | ~~~~~~~~~~~~ |
// |     MQTT     |
// | ~~~~~~~~~~~~ |

#define ENABLE_MQTT

#ifdef ENABLE_MQTT

#define MQTT_BROKER_URI "mqtt://your-mqtt-server"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENT_ID "esp32-thermostat"
#define MQTT_BASE_TOPIC "thermostat"
#define MQTT_DISCOVERY_PREFIX "homeassistant"

#endif