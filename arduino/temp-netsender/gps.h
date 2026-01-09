/*
LICENSE
  Copyright (C) 2026 the Australian Ocean Lab (AusOcean).

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

#pragma once

#include <optional>
#include <functional>

#include "sensor.h"

#define MAX_NMEA 83
char NMEASentence[MAX_NMEA];

class GPS : public Sensor {
public:
    GPS(int rx, int tx, std::function<void()> onFailure) : onFailure(onFailure) {
      Serial2.begin(9600, SERIAL_8N1, rx, tx);
    }

    std::optional<NetSender::Pin> read(int softwarePin) override {
      bool readGPGGA = false;
      String buf = "";

      // GPS Pin is T1.
      if (softwarePin != 1) {
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

      return NetSender::Pin{.value = strlen(NMEASentence), .data = (byte*)NMEASentence};
    }
private:
    int failures{0};
    std::function<void()> onFailure;



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
};
