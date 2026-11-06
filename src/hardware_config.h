#pragma once

#define PIN_STATUS_LED 2

#define PIN_JOYSTICK_X 39
#define PIN_JOYSTICK_Y 36

#define PIN_DISPLAY_RS 23
#define PIN_DISPLAY_E 22
#define PIN_DISPLAY_D4 21
#define PIN_DISPLAY_D5 19
#define PIN_DISPLAY_D6 18
#define PIN_DISPLAY_D7 4

#define PIN_DHT11 32

#define PIN_RELAY_HEAT 33
#define PIN_RELAY_COOL 25
#define PIN_RELAY_FAN 27

#define LCD_COLUMNS 16
#define LCD_ROWS 2

/* Safety limits are intentionally conservative until production sensors and
 * independently monitored hardware cutoffs are installed. */
#define THERMOSTAT_MIN_SETPOINT_F 50.0f
#define THERMOSTAT_MAX_SETPOINT_F 95.0f
#define THERMOSTAT_MIN_VALID_TEMP_C (-40.0f)
#define THERMOSTAT_MAX_VALID_TEMP_C 80.0f
#define THERMOSTAT_MAX_SAFE_TEMP_F 100.0f
#define THERMOSTAT_SENSOR_TIMEOUT_MS 10000
#define THERMOSTAT_MAX_CONTINUOUS_RUNTIME_MS (30 * 60 * 1000)
#define THERMOSTAT_DUTY_WINDOW_MS (60 * 60 * 1000)
#define THERMOSTAT_MAX_DUTY_MS (30 * 60 * 1000)
