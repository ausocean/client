/*
  Name:
    ethernet.hpp - Ethernet functions for the ESP Speaker.

  Authors:
    David Sutton <davidsutton@ausocean.org>

  License:
    Copyright (C) 2026 The Australian Ocean Lab (AusOcean).

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

#pragma once

#include <stdint.h>

#include "esp_event_base.h"

/**
 * @brief Event handler for ethernet events.
 */
void eth_event_handler(void *, esp_event_base_t, int32_t event_id, void *event_data);

/**
 * @brief Event handler for IP events.
 */
void got_ip_event_handler(void *, esp_event_base_t, int32_t, void *event_data);

/**
 * @brief Initialise ethernet MAC, PHY and TCP/IP.
 */
void init_ethernet();
