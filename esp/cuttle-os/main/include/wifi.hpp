/*
  Name:
    wifi.hpp - wifi implementation for CuttleOS.

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

#include "esp_err.h"

#include "netsender.hpp"

/**
 * @brief Initialises WiFi
 *
 * @returns err
 */
esp_err_t init_wifi();

/**
 * @brief turns off WiFi.
 *
 * wifi_off can be used to turn off wifi, and prevent reconnections. This can be
 * used to temporarily disable wifi whilst making an ADC reading.
 *
 * @returns err
 */
esp_err_t wifi_off();

/**
 * @brief turns on and reconnects WiFi.
 *
 * wifi_on can be used to turn on wifi after it has been previously configured
 * using init_wifi.
 *
 * @returns err
 */
esp_err_t wifi_on();

/**
 * @brief sets ssid and password for WiFi connections
 *
 * uses a comma separated ssid and pass (<SSID>,<PASS>)
 *
 * @returns err
 */
esp_err_t wifi_set_credentials(const char *credentials);
