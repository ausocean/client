/*
  Name:
    ota_tcp.cpp - TCP Implementation for Over-The-Air (OTA) updates.

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

#include "include/ota_tcp.hpp"
#include "cc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include <cstddef>

constexpr auto OTA_TCP_VERSION = "0.0.1";

static const constexpr auto TAG = "ota";

static volatile bool ota_initialised = false;

constexpr auto OTA_TCP_PORT = 4141;

// OTA TCP listener task.
void ota_tcp_task(void *)
{
    // Create a TCP socket.
    int ota_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(OTA_TCP_PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(ota_socket, (struct sockaddr *) &address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "failed to bind socket");
        close(ota_socket);
        return;
    }

    ESP_LOGI(TAG, "OTA now listening on port %d", OTA_TCP_PORT);
    ota_initialised = true;
    listen(ota_socket, 1);

    while (true) {
        auto req_socket = accept(ota_socket, NULL, NULL);
        ESP_LOGI(TAG, "OTA triggered, receiving update");


        auto update_partition = esp_ota_get_next_update_partition(esp_ota_get_running_partition());
        esp_ota_handle_t ota_handle;
        auto err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to begin OTA update: %s", esp_err_to_name(err));
            close(ota_socket);
            return;
        }

        static char ota_buf[1500];
        int len;
        errno = 0;
        while (true) {
            len = recv(req_socket, ota_buf, sizeof(ota_buf), 0);

            if (len > 0) {
                ESP_LOGI(TAG, "received %d bytes", len);
                err = esp_ota_write(ota_handle, ota_buf, len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "unable to write OTA update: %s", esp_err_to_name(err));
                    break;
                }
            } else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed by peer (EOF)");
                ESP_LOGI(TAG, "Attempting OTA update...");
                err = esp_ota_end(ota_handle);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "OTA Partition is valid, rebooting");
                    if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
                        ESP_LOGE(TAG, "unable to set boot partition to update partition: %s", esp_err_to_name(err));
                        break;
                    }
                    esp_restart();
                }
                ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
                break;
            } else {
                ESP_LOGE(TAG, "recv failed! return: %d, errno: %d (%s)", len, errno, strerror(errno));
                break;
            }
        }
        close(req_socket);
    }

    close(ota_socket);
    vTaskDelete(NULL);
    return;
}

void init_ota_tcp()
{
    // Guard to ensure only one update listener is created.
    if (ota_initialised) {
        ESP_LOGI(TAG, "OTA TCP listener already initialised");
        return;
    }

    ESP_LOGI(TAG, "OTA TCP version: %s", OTA_TCP_VERSION);

    // Run in freeRTOS task.
    xTaskCreatePinnedToCore(ota_tcp_task, "ota", 8192, NULL, 5, NULL, 1);
}
