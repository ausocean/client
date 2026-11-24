/*
  Name:
    controller_board.hpp - abstract hardware info for the controller PCB.

  Authors:
    David Sutton <davidsutton@ausocean.org>

  License:
    Copyright (C) 2026 The Australian Ocean Lab (AusOcean).

    This file is part of CuttleOS. CuttleOS is free software: you can
    redistribute it and/or modify it under the terms of the GNU
    General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option)
    any later version.

    CuttleOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CuttleOS in gpl.txt.  If not, see
    <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdint>
#include <optional>

// Pre-definition.
class ControllerBoard;

// Singleton instance of controller board hardware.
extern ControllerBoard hw;

/**
 * @brief hardware abstraction for the controller board
 *
 * Defines the interface for reading sensors, and controlling common actuators.
 */
class ControllerBoard {
  public:
    /** @brief default constructor */
    ControllerBoard();

    /**
     * @brief gets the current battery voltage
     *
     * @returns the calibrated battery voltage where the returned value is an
     * integer where the LSB represents 10mV.
     */
    std::optional<int32_t> battery_voltage();
};
