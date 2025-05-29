/*
  Description:
    NetSender offline request handler.
    Data is written to an SD card.

  License:
    Copyright (C) 2025 The Australian Ocean Lab (AusOcean).

    This file is part of NetSender. NetSender is free software: you can
    redistribute it and/or modify it under the terms of the GNU
    General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option)
    any later version.

    NetSender is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NetSender in gpl.txt.  If not, see
    <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <SPI.h>
#include <SD.h>

#include "NetSender.h"

namespace NetSender {

#ifdef ESP8266
#define SD_CS_PIN     15 // SD Chip Select pin (but has boot restrictions)
#define SPI_SCLK_PIN  14 // SPI Serial Clock pin
#define SPI_MISO_PIN  12 // SPI Master In Slave Out pin
#define SPI_MOSI_PIN  13 // SPI Master Out Slave In pin
#endif
#ifdef ESP32 || defined __linux__
#define SD_CS_PIN      5 // SD Chip Select pin
#define SPI_SCLK_PIN  18 // SPI Serial Clock pin
#define SPI_MISO_PIN  19 // SPI Master In Slave Out pin
#define SPI_MOSI_PIN  23 // SPI Master Out Slave In pin
#endif

// SD data file constants.
namespace datafile {
  const long version                = 1;
  const unsigned long versionMarker = 0x7ffffffe;
  const unsigned long timeMarker    = 0x7fffffff;
}

// Scalar type without ID.
typedef struct {
  long value;
  unsigned long timestamp;
} Scalar;

// Offline mode initialization.
// Initialize the SPI interface and the SD card.
bool OfflineHandler::init() {
  log(logDebug, "Initializing offline handler");
  initialized = false;
  SPI.begin(SPI_SCLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    log(logError, "Could not initialize SD card using CS pin %d", SD_CS_PIN);
    return false;
  }
  time = 0;
  initialized = true;
  return true;
}

// writeHeader writes an SD card data file header comprising a version
// record followed by the start time.
bool writeHeader(const char* name, File file) {
  Scalar datum;
  datum.value = datafile::versionMarker;
  datum.timestamp = datafile::version;
  if (file.write((byte*)&datum, sizeof(Scalar)) != sizeof(Scalar)) {
    log(logError, "Could not write version to SD card file %s", name);
    return false;
  }

  datum.value = datafile::timeMarker;
  datum.timestamp = RefTimestamp;
  if (RefTimestamp == 0) {
    log(logWarning, "RefTimestamp not set");
  }
  if (file.write((byte*)&datum, sizeof(Scalar)) != sizeof(Scalar)) {
    log(logError, "Could not write reference time to SD card file %s", name);
    return false;
  }

  return true;
}

// Offline request handler.
// Requests are handled as follows:
// - Config & Vars: delegated to the online handler, which will fail unless there is network connectivity.
// - Poll: write input data to SD card.
// - Act: does nothing.
bool OfflineHandler::request(RequestType req, Pin * inputs, Pin * outputs, bool * reconfig, String& reply) {
  switch (req) {
  case RequestConfig:
  case RequestVars:
    {
      auto h = Handlers.get(mode::Online);
      if (h == NULL) {
        log(logError, "Could not get online handler");
        return false;
      }
      auto ok = h->request(req, NULL, NULL, reconfig, reply);
      h->disconnect();
      return ok;
    }
  case RequestPoll:
    break; // Handle below.
  case RequestAct:
    return true;
  default:
    return false;
  }

  if (inputs == NULL) {
    return true; // Nothing to do.
  }

  auto ok = true;
  auto t = millis()/1000;
  Scalar datum;
  for (int ii = 0; ii < MAX_PINS && inputs[ii].name[0] != '\0'; ii++) {
    if (inputs[ii].value < 0) {
      log(logDebug, "Not saving negative value for %s", inputs[ii].name);
      continue;
    }

    log(logDebug, "Saving %s=%d @ %lu", inputs[ii].name, inputs[ii].value, t);
    if (!initialized) {
      continue;
    }

    // Append data to a binary file with the name of the pin.
    auto file = SD.open(inputs[ii].name, FILE_WRITE);
    if (file == NULL) {
      log(logError, "Could not open %s on SD card", inputs[ii].name);
      ok = false;
      continue;
    }

    if (file.size() == 0) {
      ok = writeHeader(inputs[ii].name, file);
    } else if (t < time) {
      // We've rolled over; write new reference time.
      datum.value = datafile::timeMarker;
      datum.timestamp = RefTimestamp;
       if (file.write((byte*)&datum, sizeof(Scalar)) != sizeof(Scalar)) {
        log(logError, "Could not write reference time to SD card file %s", inputs[ii].name);
        ok = false;
      }
    }

    if (ok) {
      datum.value = inputs[ii].value;
      datum.timestamp = t;
       if (file.write((byte*)&datum, sizeof(Scalar)) != sizeof(Scalar)) {
        log(logError, "Could not write data to SD card file %s", inputs[ii].name);
        ok = false;
      }
    }

    file.close();
  }

  time = t;
  return ok;
}

} // end namespace
