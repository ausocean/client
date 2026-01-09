/*
NAME
  temp-netsender - a NetSender client to send temperature sensor data to NetReceiver.

DESCRIPTION
  temp-netsender is a NetSender client to send temperature sensor data
  to NetReceiver, using the DHT, OneWire and DallasTemperature
  libraries.

  The "external" pins are mapped as follows:

    X50 = DHT temperature
    X51 = DHT humidity
    X60 = Dallas temperature (DS18B20, etc.)
    X70 = TSL2951 Light Sensor
    T1  = GPS GPGGA

  Temperatures are reported in degrees Kelvin times 10. Humidity is
  reported as a percentage times 10, i.e., 482 for 48.2% A value of -1
  is returned upon failure.

LICENSE
  Copyright (C) 2019-2025 the Australian Ocean Lab (AusOcean).

  This is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU General Public License
  along with NetSender in gpl.txt.  If not, see
  <http://www.gnu.org/licenses/>.
*/

// Uncomment sensor enable flags to enable
#define ENABLE_DHT_SENSOR
#define ENABLE_DALLAS_TEMP_SENSOR
#define ENABLE_TSL2591_SENSOR
#define ENABLE_GPS_SENSOR


// If DHT enabled, define type and hardware pins depending on ESP8266 or ESP32
#ifdef ENABLE_DHT_SENSOR
  #include "DHT.h"
  #define DHTTYPE DHT22 // external device #5

  #ifdef ESP8266
    #define DHTPIN       12
  #else
    #define DHTPIN       36
  #endif
#endif

// If Dallas Temperature enabled, define hardware pin depending on ESP8266 or ESP32
#ifdef ENABLE_DALLAS_TEMP_SENSOR
  #include "dallas_temp.h"
  #ifdef ESP8266
    #define DTPIN        13
  #else
    #define DTPIN        39
  #endif
#endif

// If TSL2591 Photometer enabled, define hardware pins (only for ESP32)
#ifdef ENABLE_TSL2591_SENSOR
  #include "tsl2591.h"
  #ifdef ESP32
    #define SDA          16
    #define SCL          17
  #endif
#endif

// If GPS enabled, define hardware pins depending on ESP8266 or ESP32
#ifdef ENABLE_GPS_SENSOR
  #include "gps.h"
  #ifdef ESP8266
    #define RXPIN        3
    #define TXPIN        -1
  #else
    #define RXPIN        34
    #define TXPIN        -1
  #endif
#endif


#include <vector>
#include "NetSender.h"

#include "sensor.h"

// Vector to store all software sensor (X pins) objects
std::vector<Sensor*> softwareSensors;

// Vector to store all binary Sensors (T pins) objects
std::vector<Sensor*> binarySensors;

// reader is the pin reader that polls available software sensors
std::optional<int> softwareReader(NetSender::Pin *pin) {
  pin->value = std::nullopt;
  if (pin->name[0] != 'X') {
    return std::nullopt;
  }
  auto requestedPin = atoi(pin->name + 1);
  for (Sensor* sensor : softwareSensors) {
    auto _pin = sensor->read(requestedPin);
    if(_pin.has_value()) {
      pin->value = _pin.value().value;
      pin->data = _pin.value().data;
      break;
    }
  }

  return pin->value;
}

std::optional<int> binaryReader(NetSender::Pin *pin) {
  pin->value = std::nullopt;
  if (pin->name[0] != 'T') {
    return std::nullopt;
  }
  auto requestedPin = atoi(pin->name +1);
  for (Sensor* sensor : binarySensors) {
    auto _pin = sensor->read(requestedPin);
    if (_pin.has_value()) {
      pin->value = _pin.value().value;
      pin->data = _pin.value().data;
      break;
    }
  }

  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP comes out of a deep sleep
void setup() {
  #ifdef ENABLE_DHT_SENSOR
    softwareSensors.push_back(new DHT(DHTPIN, DHTTYPE, []() { Serial.println("DHT exceeded failures, restarting!");}));
  #endif

  #ifdef ENABLE_DALLAS_TEMP_SENSOR
    softwareSensors.push_back(new DallasTemp(DTPIN, []() { Serial.println("Dallas temp exceeded failures, restarting!");}));
  #endif

  #ifdef ENABLE_TSL2591_SENSOR
    softwareSensors.push_back(new TSL2951(SDA, SCL, []() { Serial.println("TSL2951 Photometer exceeded failures, restarting!");}));
  #endif

  #ifdef ENABLE_GPS_SENSOR
    binarySensors.push_back(new GPS(RX, TX, []() { /* GPS Doesn't currently Count failures. */ }));
  #endif

  NetSender::ExternalReader = &softwareReader;
  NetSender::PostReader = &binaryReader;
  NetSender::init();
  loop();
}

void loop() {
  auto varsum{0};
  while (!NetSender::run(&varsum)) {
    ;
  }
}
