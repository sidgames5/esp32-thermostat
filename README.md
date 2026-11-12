# ESP32 Thermostat

Fully functional smart thermostat using only commonly available components.

## Quick Start

1. Assemble the components on the breadboard according to the instructions (TBD).
2. Clone this repository.
3. Rename `src/Configuration_example.h` to `src/Configuration.h` and fill out all the fields to fit your setup.
4. Set the pin numbers in `src/hardware_config.h` to match the physical layout of your specific ESP32 board configuration.
5. Check `platformio.ini` to make sure your ESP32 board has an environment there. If not, please add it and submit a pull request.
6. Run `pio run -e [environment] --target upload`


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

The displayed and control temperature is the average of DHT11 samples collected
over 10 seconds. If that averaged temperature does not change while heating or cooling is active
for the configured no-change interval, the firmware turns off all relays,
displays `THERMAL RUNAWAY` on the LCD, and latches the fault. The thermostat
ignores further control events until power is removed and restored.

If the LCD remains on `Waiting for data`, check that the DHT11 data line is
connected to GPIO 32 and has a pull-up resistor. The firmware retries failed
sensor reads and rate-limits the corresponding serial warning; a successful
reading is logged as `DHT11 reading recovered`.

**IMPORTANT**: Rename `src/Configuration_example.h` to `src/Configuration.h` and fill out all the fields in there before building

## Building Automation

Home Assistant support is working through MQTT. You can configure Wi-Fi credentials and MQTT broker details in `src/Configuration.h`.

Protocols to be supported in the future: BACnet/IP, Matter/IP, Zigbee (on supported ESP32 models), Matter/Thread (on supported ESP32 models)
