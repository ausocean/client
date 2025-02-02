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

#ifndef SENSOR_H
#define SENSOR_H

#include <optional>

class Sensor {
public:
    // read returns the value of the sensor at the given software pin.
    // If the sensor is not connected to the given software pin, or there are issues
    // it returns std::nullopt.
    // A sensor can handle multiple software pin values (if there are sub-sensors).
    virtual std::optional<int> read(int softwarePin) = 0;
};

#endif // SENSOR_H