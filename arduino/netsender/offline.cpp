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
  constexpr char dirNSD[]          = "/NSD";     // NetSender Data.
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

  // Check NSD directory exists, or create it.
  File dir = SD.open(datafile::dirNSD);
  if (!dir) {
    log(logDebug, "Making SD card data directory %s", datafile::dirNSD);
    if (!SD.mkdir(datafile::dirNSD)) {
      log(logError, "Could not create data directory %s on SD card", datafile::dirNSD);
      return false;
    }
  } else {
    if (!dir.isDirectory()) {
      log(logError, "SD card %s is not a directory", datafile::dirNSD);
      dir.close();
      return false;
    }
    dir.close();
  }

  initialized = true;
  log(logInfo, "Initialized SD card using CS pin %d", SD_CS_PIN);
  log(logInfo, "Writing NetSender data to SD card directory %s when offline", datafile::dirNSD);
  return true;
}

// writeRecord appends one record to a data file.
bool writeRecord(File file, long value, unsigned long timestamp) {
  Scalar record = { .value = value, .timestamp = timestamp };

  return file.write((byte*)&record, sizeof(Scalar)) == sizeof(Scalar);
}

// writeHeader writes an SD card data file header comprising a version
// record followed by the reference timestamp.
bool writeHeader(File file) {
  if (!(writeRecord(file, datafile::versionMarker, datafile::version) &&
        writeRecord(file, datafile::timeMarker, RefTimestamp))) {
    log(logError, "Could not write header to SD card file %s", file.name());
    return false;
  }

  file.flush();
  log(logDebug, "Wrote header to SD card file %s", file.name());
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

  if (!initialized) {
    if (Error != error::SDCardFailure) {
      log(logWarning, "SD card not initialized");
      setError(error::SDCardFailure);
    }
    return false;
  }

  auto ok = true;
  unsigned long t = (millis()+500)/1000; // Nearest second.
  char filename[sizeof(datafile::dirNSD)+PIN_SIZE+1];

  for (int ii = 0; ii < MAX_PINS && inputs[ii].name[0] != '\0'; ii++) {
    if (!inputs[ii].value.has_value()) {
      log(logDebug, "Not saving null value for %s", inputs[ii].name);
      continue;
    }

    log(logDebug, "Saving %s=%d @ %lu (%lu+%lu)", inputs[ii].name, inputs[ii].value.value(), RefTimestamp+t, RefTimestamp, t);

    // Append data to a binary file with the name of the pin.
    strcpy(filename, datafile::dirNSD);
    strcat(filename, "/");
    strcat(filename, inputs[ii].name);
    auto file = SD.open(filename, FILE_APPEND);
    if (!file) {
      log(logError, "Could not open %s on SD card", filename);
      ok = false;
      continue;
    }

    auto sz = file.size();
    log(logDebug, "SD card file=%s, size=%lu, t=%lu", filename, sz, t);
    if (sz == 0) {
      if (!writeHeader(file)) {
        file.close();
        ok = false;
        continue;
      }
    }

    if (!writeRecord(file, inputs[ii].value.value(), RefTimestamp+t)) {
      log(logError, "Could not write data to SD card file %s", filename);
      ok = false;
    }

    file.close();
  }

  return ok;
}

} // end namespace
