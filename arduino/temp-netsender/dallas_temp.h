/*
LICENSE
  Copyright (C) 2025 the Australian Ocean Lab (AusOcean).

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

#ifndef DALLAS_TEMP_H
#define DALLAS_TEMP_H

#include <OneWire.h>
#include <DallasTemperature.h>
#include <optional>
#include <functional>

#include "NetSender.h"
#include "sensor.h"

#define MAX_FAILURES 10

#define ZERO_CELSIUS 273.15 // In Kelvin.

class DallasTemp : public Sensor {
public:
    DallasTemp(int hardwarePin, std::function<void()> onFailure) : ow(hardwarePin), dt(&ow), onFailure(onFailure) {
        dt.begin();
    }

    std::optional<NetSender::Pin> read(int softwarePin) override {
        if (failures >= MAX_FAILURES) {
            onFailure();
            dt.begin();
            failures = 0;
        }

        if (softwarePin == 60) {
            dt.requestTemperatures();
            auto ff = dt.getTempCByIndex(0);
            if (isnan(ff) || ff <= -127) {
                failures++;
                return std::nullopt;
            } else {
                return NetSender::Pin{.value = 10 * (ff + ZERO_CELSIUS)};
            }
        }
        return std::nullopt;
    }
private:
    int failures{0};
    DallasTemperature dt;
    OneWire ow;
    std::function<void()> onFailure;
};

#endif // DALLAS_TEMP_H
