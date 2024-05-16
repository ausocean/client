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

#include "NetSender.h"
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>


#define MAX_FAILURES 10
#define DHTPIN       12
#define DTPIN        13
#define ZERO_CELSIUS 273.15 // In Kelvin.

#define DHTTYPE DHT22 // external device #5 
DHT dht(DHTPIN, DHTTYPE);
OneWire onewire(DTPIN);
DallasTemperature dt(&onewire); // external device #6

int varsum = 0;
int failures = 0;

// tempReader is the pin reader that polls either the DHT or Dallas Temperature device
int tempReader(NetSender::Pin *pin) {
  pin->value = -1;
  if (pin->name[0] != 'X') {
    return -1;
  }
  if (failures >= MAX_FAILURES) {
    if (NetSender::Debug) Serial.println(F("Reinializing DHT and DT sensors"));
    dht.begin();
    dt.begin();
    failures = 0;
  }
  int pn = atoi(pin->name + 1);
  float ff;
  switch(pn) {
  case 50: // DHT air temperature
    ff = dht.readTemperature();
    if (isnan(ff)) {
      failures++;
      return -1;
    } else {
      pin->value = 10 * (ff + ZERO_CELSIUS);
      break;
    }
  case 51: // DHT humidity
    ff = dht.readHumidity();
    if (isnan(ff)) {
      failures++;
      return -1;
    } else {
      pin->value = 10 * ff;
      break;
    }
  case 60: // Dallas temperature
    dt.requestTemperatures();
    ff = dt.getTempCByIndex(0);
    if (isnan(ff) || ff <= -127) {
      failures++;
      return -1;
    } else {
      pin->value = 10 * (ff + ZERO_CELSIUS);
      break;
    }
  default:
    return -1; 
  }
  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup() {
  dht.begin();
  dt.begin();
  NetSender::ExternalReader = &tempReader;
  NetSender::init();
  loop();
}

void loop() {
  while (!NetSender::run(&varsum)) {
    ;
  }
}
