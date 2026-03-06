/*
  Name:
    log.hpp - Functions for managing logging.

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

#include <cstdarg>
#include <cstdio>
#include "esp_log_write.h"
#include "lwip/sockets.h"

static auto log_socket = -1;
static struct sockaddr_in dest_addr;

// Broadcast logging on port 4040.
const constexpr auto LOG_UDP_IP = "255.255.255.255";
const constexpr auto LOG_UDP_PORT = 4040;

inline int udp_logging_vprintf(const char *fmt, va_list args)
{
    char log_buf[512]; // Increased size: ANSI codes add ~15 bytes per line

    // This captures the string EXACTLY as ESP_LOG intended, colors and all
    int len = vsnprintf(log_buf, sizeof(log_buf), fmt, args);

    if (len > 0 && log_socket >= 0) {
        // Send the raw buffer including the hidden escape codes
        sendto(log_socket, log_buf, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    // Still print to local serial
    return vprintf(fmt, args);
}

inline void init_udp_logging()
{
    // Create the UDP socket
    log_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    dest_addr.sin_addr.s_addr = inet_addr(LOG_UDP_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(LOG_UDP_PORT);

    // Set the log hook
    esp_log_set_vprintf(udp_logging_vprintf);
}
