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

#include "include/log_udp.hpp"

// Logging Tag.
static constexpr auto TAG = "log";

// Queue item size (max CONFIG_LOG_LEN characters per line)
static constexpr auto QUEUE_ITEM_SIZE = CONFIG_LOG_LEN * sizeof(char);

// Storage buffer for logs.
static uint8_t queue_storage[CONFIG_LOG_QUEUE_LEN * (QUEUE_ITEM_SIZE)];
static StaticQueue_t queue_buf;
QueueHandle_t log_queue = NULL;

// Original vprintf handler so we can still print directly to stdout/serial
static vprintf_like_t default_vprintf = nullptr;

// custom intercept for logs sent through ESP logging pipeline.
static int log_vprintf(const char *fmt, va_list args)
{
    char log_buf[CONFIG_LOG_LEN];

    // Format the incoming log string
    int len = vsnprintf(log_buf, sizeof(log_buf), fmt, args);

    // Write to standard stdout/UART immediately if original handler is preserved
    if (default_vprintf) {
        default_vprintf(fmt, args);
    }

    // Push formatted string to the log queue
    if (log_queue != nullptr && len > 0) {
        // Use 0 ticks to prevent blocking calling tasks if the queue gets full
        xQueueSend(log_queue, log_buf, 0);
    }

    return len;
}

// Task to handle logs sent through the queue. This is handled asynchronously
// to logging to prevent blocking network and file writes.
static void log_processor_task(void *pvParameters)
{
    char msg[CONFIG_LOG_LEN];

    while (true) {
        // Block until a new log message arrives in the queue.
        if (xQueueReceive(log_queue, msg, portMAX_DELAY) == pdTRUE) {
            udp_log_send(msg);
            // TODO: Log to file.
        }
    }
}

// Initialises logging queue which is used to queue logs to prevent blocking
// calls to file or network.
esp_err_t init_log_queue()
{
    // Create static queue.
    log_queue = xQueueCreateStatic(CONFIG_LOG_QUEUE_LEN, CONFIG_LOG_LEN * sizeof(char), queue_storage, &queue_buf);

    if (log_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create static log queue");
        return ESP_FAIL;
    }

    return ESP_OK;
}

std::function<std::optional<std::vector<uint8_t>>()> init_logging()
{
    // Initialise queue.
    auto err = init_log_queue();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to init log queue: %s", esp_err_to_name(err));
        return NULL;
    }

    // Initialise UDP logging.
    init_udp_logging();

    // TODO: Initialise file logging.

    // Start logging task.
    if (xTaskCreatePinnedToCore(log_processor_task, "logging_task", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "log task creation failed");
        return NULL;
    }

    default_vprintf = esp_log_set_vprintf(log_vprintf);

    p = std::make_unique<Pipi::FileLogger>("/sdcard/logs");

    return []() -> std::optional<std::vector<uint8_t>> {
        auto &stream = p->get_logs();

        std::vector<uint8_t> buffer(1024); // Allocate space for logs

        // Read logs from stream into buffer
        stream.read(reinterpret_cast<char *>(buffer.data()), buffer.size());

        auto bytes_read = stream.gcount();
        if (bytes_read == 0) {
            return std::nullopt;
        }

        buffer.resize(bytes_read); // Shrink to actual payload size
        return buffer;
    };
}
