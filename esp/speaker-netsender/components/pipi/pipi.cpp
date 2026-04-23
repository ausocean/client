/*
  Name:
    pipi.cpp - An ESP-IDF component to implement a logging system.

  Description:
    See https://www.cloudblue.org

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

#include "include/pipi.hpp"

#include <cinttypes>
#include <cstring>

#include "esp_err.h"

Pipi::Entry::Entry(const int64_t ts, const Level level, const char *msg) : timestamp(ts), level(level), data(msg) {}

esp_err_t Pipi::Entry::write(std::ostream &stream)
{
    int len = strlen(data);
    if (len > this->MAX_LOG_LENGTH) {
        len = this->MAX_LOG_LENGTH;
    } else if (len < 0) {
        return ESP_FAIL;
    }
    char marshalled[Pipi::Entry::MAX_LOG_LENGTH + 100];
    auto written = snprintf(marshalled, Pipi::Entry::MAX_LOG_LENGTH + 100,
                            "{"
                            "\"timestamp\":%" PRId64 ","
                            "\"level\":%d,"
                            "\"message\":\"%s\""
                            "}",
                            this->timestamp, this->level, this->data);
    if (written < 0 || written >= sizeof(marshalled)) {
        return ESP_FAIL;
    }

    stream.write(marshalled, written);

    return ESP_OK;
}
