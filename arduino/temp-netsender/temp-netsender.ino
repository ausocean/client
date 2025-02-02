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

// isValidNMEA returns true if the NMEA sentence is valid.
bool isValidNMEA(String& sentence) {
  sentence.trim();
  if (sentence.isEmpty()) {
    return false;
  }
  if (sentence[0] != '$') {
    return false;
  }
  int checksumPos = sentence.indexOf('*');
  if (checksumPos == -1) {
    return false;
  }
  if (sentence.substring(checksumPos+1).length() < 2) {
    return false;
  }
  // Extract the supplied checksum.
  unsigned int suppliedChecksum = strtol(sentence.substring(checksumPos+1, checksumPos+3).c_str(), NULL, 16);

  // Compute the checksum by XORing everything between the '$' and '*'.
  unsigned int computedChecksum = 0;
  for (int i = 1; i < checksumPos; i++) {
    computedChecksum ^= sentence.charAt(i);
  }

  return (computedChecksum == suppliedChecksum);
}

// gpsReader reads GPS data on Serial 2 and sets the T1 pin value to the most recent GPGGA sentence,
// or -1 otherwise.
std::optional<int> gpsReader(NetSender::Pin *pin) {
  bool readGPGGA = false;
  String buf = "";

  pin->value = std::nullopt;
  if (strcmp(pin->name, "T1") != 0) {
    return std::nullopt;
  }

  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '$') {
      // Start buffering a new sentence.
      buf = "";
      buf += c;

    } else if (c == '\n') {
      buf += c;
      if (buf.startsWith("$GPGGA") && isValidNMEA(buf)) {
       // We have a valid GPGGA sentence, so save it.
       strcpy(NMEASentence, buf.c_str());
       readGPGGA = true;
      }
      buf = "";

    } else if (buf.length() > 0) {
      // Append only if we are currently buffering.
      buf += c;
    }

    // Ignore malformed NMEA sentences.
    if (buf.length() >= MAX_NMEA) {
      buf = "";
    }
  }

  if (!readGPGGA) {
    return std::nullopt;
  }

  pin->value = strlen(NMEASentence);
  pin->data = (byte*)NMEASentence;
  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP comes out of a deep sleep
void setup() {
  Wire.begin(16,17);
  tsl.setGain(TSL2591_GAIN_LOW);
  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  tsl.begin();
  Serial2.begin(9600, SERIAL_8N1, RXPIN, TXPIN);
  NetSender::ExternalReader = &tempReader;
  NetSender::PostReader = &gpsReader;
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
