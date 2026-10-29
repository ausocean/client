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
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

#include "include/log_udp.hpp"
#include "netsender.hpp"
#include "pipi.hpp"

// Logging Tag.
static constexpr auto TAG = "log";

// Queue item size (max CONFIG_LOG_LEN characters per line)
static constexpr auto QUEUE_ITEM_SIZE = CONFIG_LOG_LEN * sizeof(char);

// Logging is kept off the audio core and below the audio task priority.
static constexpr const auto LOG_TASK_CORE = 0;
static constexpr const auto LOG_TASK_PRIORITY = 2;

// Logs waiting to be uploaded are kept in RAM so the HTTP poll path never
// touches the SD card.
static constexpr const auto LOG_UPLOAD_BUF_SIZE = 8192;
static constexpr const auto LOG_JSON_MAX = 512;
static constexpr const auto LOG_ROTATE_COUNT = 512;

// Storage buffer for logs.
static uint8_t queue_storage[CONFIG_LOG_QUEUE_LEN * (QUEUE_ITEM_SIZE)];
static StaticQueue_t queue_buf;
QueueHandle_t log_queue = NULL;

std::unique_ptr<Pipi::FileLogger> p;

static char upload_buf[LOG_UPLOAD_BUF_SIZE];
static size_t upload_len = 0;
static std::mutex upload_mutex;

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
    char json[LOG_JSON_MAX];
    uint32_t logs_since_rotate = 0;

    while (true) {
        // Block until a new log message arrives in the queue.
        if (xQueueReceive(log_queue, msg, portMAX_DELAY) == pdTRUE) {
            udp_log_send(msg);

            auto err = p->log(msg, json, sizeof(json));
            if (err == ESP_OK && json[0] != '\0') {
                const auto len = strlen(json);
                std::lock_guard<std::mutex> lock(upload_mutex);
                if (upload_len + len <= sizeof(upload_buf)) {
                    memcpy(upload_buf + upload_len, json, len);
                    upload_len += len;
                }
            }

            // Rotate the on-disk log periodically. Uploading is served from
            // the RAM buffer above, so this does not affect the HTTP path.
            if (++logs_since_rotate >= LOG_ROTATE_COUNT) {
                logs_since_rotate = 0;
                p->get_logs();
            }
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

static std::optional<std::vector<uint8_t>> read_logs()
{
    std::lock_guard<std::mutex> lock(upload_mutex);
    std::vector<uint8_t> buffer(upload_buf, upload_buf + upload_len);
    upload_len = 0;
    return buffer;
}

log_read_fn init_logging()
{
    // Initialise queue.
    auto err = init_log_queue();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to init log queue: %s", esp_err_to_name(err));
        return nullptr;
    }

    // Initialise UDP logging.
    init_udp_logging();

    // Start logging task.
    if (xTaskCreatePinnedToCore(log_processor_task, "logging_task", 4096, NULL, LOG_TASK_PRIORITY, NULL,
                                LOG_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "log task creation failed");
        return nullptr;
    }

    p = std::make_unique<Pipi::FileLogger>("/sdcard/logs");

    default_vprintf = esp_log_set_vprintf(log_vprintf);

    return read_logs;
}
