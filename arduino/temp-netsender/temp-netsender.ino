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
    
  Temperatures are reported in degrees Kelvin times 10. Humidity is
  reported as a percentage times 10, i.e., 482 for 48.2% A value of -1
  is returned upon failure.

SEE ALSO
  NetReceiver help: http://netreceiver.appspot.com/help.

LICENSE
  Copyright (C) 2019 the Australian Ocean Lab (AusOcean).

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


#include <vector>
#include "NetSender.h"

#include "sensor.h"

// Vector to store all sensor objects
std::vector<Sensor*> sensors;

// reader is the pin reader that polls available sensors
std::optional<int> reader(NetSender::Pin *pin) {
  pin->value = std::nullopt;
  if (pin->name[0] != 'X') {
    return std::nullopt;
  }

  auto requestedPin = atoi(pin->name + 1);
  for (Sensor* sensor : sensors) {
    auto res = sensor->read(requestedPin);
    if(res.has_value()) {
      pin->value = res;
      break;
    }
  }

  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup() {
  #ifdef ENABLE_DHT_SENSOR
    sensors.push_back(new DHT(DHTPIN, DHTTYPE, []() { Serial.println("DHT exceeded failures, restarting!");}));
  #endif

  #ifdef ENABLE_DALLAS_TEMP_SENSOR
    sensors.push_back(new DallasTemp(DTPIN, []() { Serial.println("Dallas temp exceeded failures, restarting!");}));
  #endif

  NetSender::ExternalReader = &reader;
  NetSender::init();
  loop();
}

void loop() {
  auto varsum{0};
  while (!NetSender::run(&varsum)) {
    ;
  }
}
