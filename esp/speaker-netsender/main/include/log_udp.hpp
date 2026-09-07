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

#include "cc.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static auto log_socket = -1;
static struct sockaddr_in dest_addr;

// Broadcast logging on port 4040.
const constexpr auto LOG_UDP_IP = "255.255.255.255";
const constexpr auto LOG_UDP_PORT = 4040;

// Broadcasts the passed message over the configured UDP logging port.
inline int udp_log_send(const char *msg)
{
    auto len = strlen(msg);

    if (log_socket >= 0) {
        return sendto(log_socket, msg, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    return -1;
}

/**
 * @brief initialise UDP logging
 */
inline void init_udp_logging()
{
    // Create the UDP socket
    log_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    // Configure destination.
    dest_addr.sin_addr.s_addr = inet_addr(LOG_UDP_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(LOG_UDP_PORT);
}
