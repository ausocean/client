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

#include <Adafruit_TSL2591.h>
#include <optional>
#include <functional>

#include "sensor.h"

#define MAX_FAILURES 10

static constexpr auto TSL_ID{70};

class TSL2951 : public Sensor {
public:
    TSL2951(int sdaPin, int sclPin, std::function<void()> onFailure) : tsl(TSL_ID), onFailure(onFailure) {
      Wire.begin(sdaPin, sclPin);
      tsl.setGain(TSL2591_GAIN_LOW);
      tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
      tsl.begin();
    }

    std::optional<NetSender::Pin> read(int softwarePin) override {
      if (failures >= MAX_FAILURES) {
            onFailure();
            tsl.begin();
            failures = 0;
        }

        if (softwarePin == 60) {
          auto lum = tsl.getLuminosity(TSL2591_FULLSPECTRUM);
              if ((lum <= 0) || isnan(lum)) {
                failures++;
                return std::nullopt;
              }
              NetSender::Pin pin{.value = lum};
              return pin;
        }
        return std::nullopt;
    }
private:
    int failures{0};
    Adafruit_TSL2591 tsl;
    std::function<void()> onFailure;
};
