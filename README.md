# ESP32 Thermostat

Fully functional smart thermostat using only commonly available components.

## Hardware

The design should work with any ESP32 that has 14 free GPIO pins and supports ESP-IDF.

The breadboard prototype currently uses dry contact relay modules but I would like the final version to use TRIACs for longevity.

Note about temperature sensing: currently the prototype uses a DHT11 and the firmware also reflects that. I understand that this is a terrible choice however its good enough for the prototype. The final version will use a more accurate sensor.

## Firmware

The firmware is written in the C programming language with the ESP-IDF HAL framework and PlatformIO as the build system.

## Building Automation

Firmware will soon support MQTT, Matter/IP, BACnet/IP, and on supported ESP32 models, Zigbee and Thread.
