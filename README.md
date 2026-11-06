# ESP32 Thermostat

Fully functional smart thermostat using only commonly available components.

## Quick Start

1. Assemble the components on the breadboard according to the instructions (TBD).
2. Clone this repository.
3. Rename `src/Configuration_example.h` to `src/Configuration.h` and fill out all the fields to fit your setup.


## Hardware

The design should work with any ESP32 that has 14 free GPIO pins and supports ESP-IDF.

The breadboard prototype currently uses dry contact relay modules but I would like the final version to use TRIACs for longevity.

Note about temperature sensing: currently the prototype uses a DHT11 and the firmware also reflects that. I understand that this is a terrible choice however its good enough for the prototype. The final version will use a more accurate sensor.

## Firmware

The firmware is written in the C programming language with the ESP-IDF HAL framework and PlatformIO as the build system.

The application is organized as FreeRTOS tasks. The joystick and DHT11 tasks
publish typed events to an application queue, while the thermostat task owns
relay and LCD state. Wi-Fi, MQTT, Zigbee, or other connectivity tasks can
publish control events through the same queue without adding more polling logic
to the thermostat task. Wi-Fi and MQTT use this integration today.

If the LCD remains on `Waiting for data`, check that the DHT11 data line is
connected to GPIO 32 and has a pull-up resistor. The firmware retries failed
sensor reads and rate-limits the corresponding serial warning; a successful
reading is logged as `DHT11 reading recovered`.

**IMPORTANT**: Rename `src/Configuration_example.h` to `src/Configuration.h` and fill out all the fields in there before building

## Building Automation

MQTT is supported when `ENABLE_MQTT` is defined in
[`src/Configuration.h`](src/Configuration.h). Configure the broker URI,
optional credentials, client ID, and base topic there. The firmware publishes
retained state to `<base>/temperature/current`, `<base>/temperature/target`,
and `<base>/mode`, and accepts commands on
`<base>/temperature/target/set` (degrees Fahrenheit) and `<base>/mode/set`
(`OFF`, `HEAT`, `COOL`, `AUTO`, or `FAN`).

Protocols to be supported in the future: Matter/IP, BACnet/IP, Zigbee (on supported ESP32 models), Matter/Thread (on supported ESP32 models)
