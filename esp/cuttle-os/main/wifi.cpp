/*
  Name:
    wifi.cpp - wifi implementation for CuttleOS.

  Authors:
    David Sutton <davidsutton@ausocean.org>

  License:
    Copyright (C) 2026 The Australian Ocean Lab (AusOcean).

    This file is part of CuttleOS. CuttleOS is free software: you can
    redistribute it and/or modify it under the terms of the GNU
    General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option)
    any later version.

    CuttleOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CuttleOS in gpl.txt.  If not, see
    <http://www.gnu.org/licenses/>.
*/

#include "include/wifi.hpp"

#include <cstdio>
#include <cstring>
#include <sys/param.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"

#include "netsender.hpp"
#include "sdkconfig.h"

// Logging Tag.
static auto TAG = "WiFi";

// Event group to handle synchronisation between tasks.
static EventGroupHandle_t s_wifi_event_group;

// Event group bit definitions.
constexpr auto WIFI_CONNECTED_BIT = BIT0;
constexpr auto WIFI_DISCONNECTED_BIT = BIT1;
constexpr auto WIFI_DISABLED = BIT2;

// Maximium string lengths in config struct.
constexpr auto WIFI_MAX_SSID_LEN = 32;
constexpr auto WIFI_MAX_PASS_LEN = 64;

static auto failure_count = 0;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        // Wifi events.
        switch (event_id) {
        case WIFI_EVENT_STA_DISCONNECTED: {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
            if (xEventGroupGetBits(s_wifi_event_group) & WIFI_DISABLED) {
                break;
            }
            auto *disc = static_cast<wifi_event_sta_disconnected_t *>(event_data);
            ESP_LOGW(TAG, "WiFi disconnected (reason %d), retrying...", disc->reason);
            failure_count++;
        }
        case WIFI_EVENT_STA_START:
            if (xEventGroupGetBits(s_wifi_event_group) & WIFI_DISABLED) {
                ESP_LOGI(TAG, "Wifi disabled, ignoring reconnect attempt");
                // We have disabled the wifi, don't reconnect.
                break;
            }
            if (failure_count >= 5) {
                failure_count = 0;
                constexpr auto MAX_CREDS_SIZE = WIFI_MAX_SSID_LEN + WIFI_MAX_PASS_LEN + 2;
                char creds[MAX_CREDS_SIZE];
                snprintf(creds, MAX_CREDS_SIZE, "%s,%s", CONFIG_WIFI_DEFAULT_SSID, CONFIG_WIFI_DEFAULT_PASS);
                wifi_set_credentials(creds);
                break;
            }
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            failure_count = 0;
            ESP_LOGI(TAG, "wifi connected");
            break;
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
            // Do nothing.
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // IP events.
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        // Unhandled events.
        ESP_LOGI(TAG, "unhandled event: %s:%d", event_base, event_id);
    }
}

esp_err_t _wifi_wait_connected()
{
    // Wait for wifi connection.
    // TODO: Consider offline mode, and when to transition.
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        ESP_LOGI(TAG, "Connected to %s", reinterpret_cast<char *>(ap_info.ssid));
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    return ESP_OK;
}

esp_err_t init_wifi()
{
    s_wifi_event_group = xEventGroupCreate();

    auto err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to init TCP/IP: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to create default event loop: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to init wifi: %s", esp_err_to_name(err));
        return err;
    }

    // Register event handler for wifi events, and for got IP events.
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id);
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_WIFI_DEFAULT_SSID,
                .password = CONFIG_WIFI_DEFAULT_PASS,

                // Default settings for WPA2.
                .threshold =
                    {
                        .authmode = WIFI_AUTH_WPA2_PSK,
                    },
                .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED,
            },
    };

    // STA mode is for device to act as a client of a wifi network.
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to set wifi mode sta: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to set wifi config: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to start wifi: %s", esp_err_to_name(err));
        return err;
    }

    return _wifi_wait_connected();
}

esp_err_t wifi_off()
{
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    xEventGroupSetBits(s_wifi_event_group, WIFI_DISABLED);
    return esp_wifi_stop();
}

esp_err_t wifi_on()
{
    xEventGroupClearBits(s_wifi_event_group, WIFI_DISABLED);
    return esp_wifi_start();
}

esp_err_t _use_default_creds(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    if (strlen(CONFIG_WIFI_DEFAULT_SSID) > ssid_size) {
        ESP_LOGW(TAG, "default credentials SSID too long");
    }
    strlcpy(ssid, CONFIG_WIFI_DEFAULT_SSID, ssid_size);

    if (strlen(CONFIG_WIFI_DEFAULT_PASS) > pass_size) {
        ESP_LOGW(TAG, "default credentials password too long");
    }
    strlcpy(ssid, CONFIG_WIFI_DEFAULT_PASS, pass_size);

    return ESP_OK;
}

esp_err_t wifi_set_credentials(const char *credentials)
{
    char ssid[WIFI_MAX_SSID_LEN + 1] = {};
    char pass[WIFI_MAX_PASS_LEN + 1] = {};

    const char *sep = (credentials != nullptr) ? strchr(credentials, ',') : nullptr;
    // Must have a comma, non-empty SSID, and at least 8-char WPA2 password.
    if (sep == nullptr || sep == credentials || strlen(sep + 1) < 8) {
        _use_default_creds(ssid, sizeof(ssid), pass, sizeof(pass));
    } else {
        size_t ssid_len = sep - credentials;
        size_t pass_len = strlen(sep + 1);

        if (ssid_len > WIFI_MAX_SSID_LEN) {
            ESP_LOGE(TAG, "SSID is too long (max %d characters)", WIFI_MAX_SSID_LEN);
            return ESP_ERR_INVALID_ARG;
        }
        if (pass_len >= WIFI_MAX_PASS_LEN) {
            ESP_LOGE(TAG, "Password is too long (max %d characters)", WIFI_MAX_PASS_LEN - 1);
            return ESP_ERR_INVALID_ARG;
        }

        memcpy(ssid, credentials, ssid_len);
        memcpy(pass, sep + 1, pass_len);
    }

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;

    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, pass, strlen(pass));

    ESP_LOGI(TAG, "trying to connect to: %s,%s", ssid, pass);

    wifi_off();

    auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to set wifi config: %s", esp_err_to_name(err));
        return err;
    }

    return wifi_on();
}
