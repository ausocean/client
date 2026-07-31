/*
  Name:
    log.cpp - logging layer to split logs between different outputs.

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

#include "include/log.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

// Logging Tag.
static constexpr auto TAG = "log";

// Queue item size (max CONFIG_LOG_LEN characters per line)
static constexpr auto QUEUE_ITEM_SIZE = CONFIG_LOG_LEN * sizeof(char);

// Storage buffer for logs.
static uint8_t queue_storage[CONFIG_LOG_QUEUE_LEN * (QUEUE_ITEM_SIZE)];
static StaticQueue_t queue_buf;
QueueHandle_t log_queue = NULL;

esp_err_t init_log_queue(void)
{
    // Create static queue.
    log_queue = xQueueCreateStatic(CONFIG_LOG_QUEUE_LEN, CONFIG_LOG_LEN * sizeof(char), queue_storage, &queue_buf);

    if (log_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create static log queue");
        return ESP_FAIL;
    }

    return ESP_OK;
}
