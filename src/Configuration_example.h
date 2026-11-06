// | ~~~~~~~~~~~~~~~~~~~~~~~ |
// |                         |
// |     CONFIGURATION.H     |
// |                         |
// | ~~~~~~~~~~~~~~~~~~~~~~~ |

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

#endif