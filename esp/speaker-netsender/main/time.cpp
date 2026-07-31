/*
  Name:
    time.cpp - Time management utilities and helpers.

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

// Logging TAG.
#include "include/time.hpp"

#include <ctime>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

// Logging TAG.
static constexpr auto TAG = "time";

void initialise_sntp()
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    auto err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP (%s)", esp_err_to_name(err));
        return;
    }

    auto retry = 0;
    const auto max_retries = 10;
    while ((err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000))) != ESP_OK && ++retry < max_retries) {
        ESP_LOGI(TAG, "Attempt to synchronise time (%d/%d) failed: %s", retry, max_retries, esp_err_to_name(err));
    }

    if (retry >= max_retries) {
        ESP_LOGE(TAG, "Failed to synchronize time within timeout!");
    } else {
        ESP_LOGI(TAG, "System time set successfully");
    }
}
