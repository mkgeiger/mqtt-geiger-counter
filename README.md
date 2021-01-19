# MQTT Geiger Counter
## Overview
In this project you will see how to build a MQTT client on base of an ESP8266 to measure the local gamma dose rate. I preferred to integrate my older Geiger Counter project (see https://github.com/mkgeiger/geiger-counter-iot) into my MQTT cloud. For that I am reusing the older hardware without any modifications. It will be used then only for long term measurements. Changes are affecting only the software, which will completely re-written to make it MQTT capable.

## Hardware design
### Schematic
Please refer to my other project: https://github.com/mkgeiger/geiger-counter-iot/blob/master/README.md#high-voltage-booster

### ESP8266 controller board
Please refer to my other project: https://github.com/mkgeiger/geiger-counter-iot/blob/master/README.md#nodemcu-esp8266-12e

### Sensor
Please refer to my other project: https://github.com/mkgeiger/geiger-counter-iot/blob/master/README.md#gm-tube

## SW-update
SW-update is done via USB cable.

## SW installation and SW build
Following steps need to be done first:
1. install Arduino IDE 1.8.1x
2. download and install the ESP8266 board supporting libraries with this URL: http://arduino.esp8266.com/stable/package_esp8266com_index.json
3. select the `NodeMCU 1.0 (ESP-12E Module)` board
4. install the `Async MQTT client` library: https://github.com/marvinroger/async-mqtt-client/archive/master.zip
5. install the `Async TCP` library: https://github.com/me-no-dev/ESPAsyncTCP/archive/master.zip
6. config (see next chapter), compile and flash

## SW configuration
The configuration is completely done in the web frontend of the WifiManager. At first startup, the software boots up in access point mode. In this mode you can configure parameters like
* Wifi SSID
* Wifi password
* MQTT broker IP address
* MQTT user
* MQTT password

## SW normal operation
The software counts the ticks produced by the gamma tubes within measurement cycles of 10 minutes. The software publishes after each measurement cycle to 2 MQTT topics: Counts Per Minute [CPM] and the derived ionizing radiation dose [μSv/h]. Also the software supports re-connection to Wifi and to the MQTT broker in case of power loss, Wifi loss or MQTT broker unavailability. The MQTT topics begin with the device specific MAC-address string (in the following "A020A600F73A" as an example). This is useful when having multiple controllers in your MQTT cloud to avoid collisions.

Publish topics:
* Topic: "/A020A600F73A/cpm"            Payload example: "22.50"
* Topic: "/A020A600F73A/usvph"          Payload example: "0.1234"


