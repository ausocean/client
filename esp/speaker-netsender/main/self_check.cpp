/*
  Name:
    self_check.cpp - Self check functions to validate hardware and
    firmware compatibility.

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

#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "include/self_check.hpp"

#include <stddef.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "include/globals.h"
#include "tas5805.hpp"

static constexpr auto TAG = "self_check";

/**
 * @brief Checks if Ethernet is physically connected and has a valid IP.
 *
 * @return true if network is ready for TCP/IP traffic, false otherwise.
 */
bool is_ethernet_connected()
{
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");

    if (eth_netif == NULL) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return false;
    }

    // Check physical link.
    bool link_up = esp_netif_is_netif_up(eth_netif);

    // Check for IP.
    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(eth_netif, &ip_info);

    if (link_up && err == ESP_OK && ip_info.ip.addr != 0) {
        ESP_LOGI(TAG, "Ethernet is VALID. IP: " IPSTR, IP2STR(&ip_info.ip));
        return true;
    }

    ESP_LOGW(TAG, "Ethernet validation failed: Link=%s, IP_Assigned=%s", link_up ? "UP" : "DOWN",
             (ip_info.ip.addr != 0) ? "YES" : "NO");

    return false;
}

/**
 * @brief Checks if a filesystem on an SD card can be reached.
 *
 * @returns true if filesystem is accessible, false otherwise.
 */
bool is_sd_connected()
{
    struct stat st;
    return stat(MOUNT_POINT, &st) == 0;
}

bool self_check_ok(TAS5805 *amp)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    bool eth_ok = is_ethernet_connected();
    bool amp_ok = amp->is_connected();
    bool sd_ok = is_sd_connected();

    bool passing = eth_ok && amp_ok && sd_ok;

    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           SYSTEM SELF-DIAGNOSTIC         ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");

    if (eth_ok) {
        ESP_LOGI(TAG, "║  Ethernet Link  : [  PASS  ]             ║");
    } else {
        ESP_LOGE(TAG, "║  Ethernet Link  : [  FAIL  ]             ║");
    }

    if (sd_ok) {
        ESP_LOGI(TAG, "║  SD Card Mount  : [  PASS  ]             ║");
    } else {
        ESP_LOGE(TAG, "║  SD Card Mount  : [  FAIL  ]             ║");
    }

    if (amp_ok) {
        ESP_LOGI(TAG, "║  Amp Response   : [  PASS  ]             ║");
    } else {
        ESP_LOGE(TAG, "║  Amp Response   : [  FAIL  ]             ║");
    }

    ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");

    if (passing) {
        ESP_LOGI(TAG, "║  OVERALL STATUS : [  READY ]             ║");
        ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    } else {
        ESP_LOGE(TAG, "║  OVERALL STATUS : [ ERROR  ]             ║");
        ESP_LOGE(TAG, "╚══════════════════════════════════════════╝");
    }

    return passing;
}
