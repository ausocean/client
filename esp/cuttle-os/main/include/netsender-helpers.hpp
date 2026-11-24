/*
  Name:
    netsender-helpers.hpp - Helper functions for NetSender implementation.

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

#pragma once

#include <cstdint>
#include <mutex>

#include "esp_log.h"

#include "controller_board.hpp"
#include "netsender.hpp"
#include "netsender_vars.hpp"
#include "wifi.hpp"

// Logging tag.
static auto TAG = "NetSender Helper";

// Device variables.
netsender::device_var_state_t vars;
std::mutex vars_mutex;

// Callback function to be registered with NetSender to parse vars response.
esp_err_t parse_vars(const char *var_resp)
{
    ESP_LOGD(TAG, "parsing variables in callback");

    // Get the ID in the response.
    constexpr const auto MAX_ID_LEN = 64;
    char id[MAX_ID_LEN];
    auto has_id = netsender_extract_json(var_resp, "id", id, sizeof(id));
    if (!has_id) {
        ESP_LOGE(TAG, "unable to get ID from var response");
        return ESP_FAIL;
    }

    netsender::device_var_state_t updated;
    {
        std::lock_guard<std::mutex> lock(vars_mutex);
        updated = vars;
    }

    // Parse the registered variables into the local state.
    char val[netsender::MAX_STR_VAR_LEN];
    char var_name[MAX_ID_LEN + 1 + netsender::MAX_STR_VAR_LEN];
    for (auto i = 0; i < netsender::VAR_COUNT; i++) {
        if (strcmp(netsender::VARIABLES[i], netsender::var::VAR_ID_VS) == 0) {
            // Varsum (vs) is not prepended with id scope.
            snprintf(var_name, sizeof(var_name), "%s", netsender::VARIABLES[i]);
        } else {
            snprintf(var_name, sizeof(var_name), "%s.%s", id, netsender::VARIABLES[i]);
        }
        ESP_LOGD(TAG, "looking for variable: %s", var_name);
        if (netsender_extract_json(var_resp, var_name, val, sizeof(val))) {
            netsender::update_state_member(updated, netsender::VARIABLES[i], val);
            ESP_LOGD(TAG, "got variable: %s=%s", var_name, val);
        }
    }

    {
        std::lock_guard<std::mutex> lock(vars_mutex);
        auto vars_changed = vars.vs != updated.vs;
        if (!vars_changed) {
            ESP_LOGW(TAG, "Varsum not changed in call to parse vars.");
            return ESP_OK;
        }
        vars = updated;
    }

    auto err = netsender::write_vars_to_nvs(vars);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to write the vars to nvs");
        return err;
    }

    ESP_LOGD(TAG, "wrote vars to file");
    return ESP_OK;
}

std::optional<int32_t> load_stored_vars()
{
    auto got_vars = netsender::read_vars_from_nvs();
    if (!got_vars.has_value()) {
        ESP_LOGW(TAG, "got no variables from NVS");
        return std::nullopt;
    }

    ESP_LOGI(TAG, "got variables from NVS");
    auto stored = got_vars.value();
    {
        std::lock_guard<std::mutex> lock(vars_mutex);
        vars = stored;
    }
    return stored.vs;
}

esp_err_t config_callback(netsender_configuration_t *config)
{
    // Update WiFi connection.
    return wifi_set_credentials(config->wifi);
}

std::optional<long long> read_battery_pin() { return hw.battery_voltage(); }
