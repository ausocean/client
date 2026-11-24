/*
  Name:
    cuttle-os.cpp - NetSender implementation for AusOcean controllers.

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

#include <cstdlib>
#include <esp_log.h>

#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

#include "controller_board.hpp"
#include "include/netsender-helpers.hpp"
#include "include/netsender_vars.hpp"
#include "include/victron_ble.hpp"
#include "include/wifi.hpp"
#include "netsender.hpp"

// Make the app c++ compatible.
extern "C" {
void app_main();
}

static constexpr auto CUTTLE_OS_VERSION = "0.1.0";

// Logging tag.
static constexpr auto LOG_TAG = "CuttleOS";

// NetSender instance.
static Netsender ns;

void app_main(void)
{
    ESP_LOGI(LOG_TAG, "CuttleOS initialising...");
    ESP_LOGI(LOG_TAG, "CuttleOS Version: %s", CUTTLE_OS_VERSION);
    ESP_LOGI(LOG_TAG, "ICD Version: %s", netsender::ICD_VERSION);
    ESP_LOGI(LOG_TAG, "NetSender Version: %s", NETSENDER_VERSION);

    ESP_LOGI(LOG_TAG, "Initialising WiFi");
    auto err = init_wifi();
    if (err != ESP_OK) {
        while (true);
    }
    err = wifi_set_credentials(ns.get_wifi_credentials());
    if (err != ESP_OK) {
        while (true);
    }
    ESP_LOGI(LOG_TAG, "WiFi initialised");

    ESP_LOGI(LOG_TAG, "Initialising HW");
    err = hw.init();
    if (err != ESP_OK) {
        while (true);
    }
    ESP_LOGI(LOG_TAG, "HW initialised");

    // Register variable parser.
    err = ns.register_variable_parser(parse_vars);
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Unable to register NetSender variable parser: %s", esp_err_to_name(err));
        while (true);
    }

    // Register config callback.
    err = ns.register_config_callback(config_callback);
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Unable to register config callback function: %s", esp_err_to_name(err));
        while (true);
    }

    // Load stored variables.
    auto vs = load_stored_vars();
    if (!vs.has_value()) {
        ESP_LOGW(LOG_TAG, "No varsum loaded from stored vars");
    } else {
        ns.set_varsum(vs.value());
    }

    // Register Battery Pin.
    char pin_name[4] = "A4";
    ns.register_input(pin_name, read_battery_pin);

    // Start NetSender Task.
    ns.start();

    while (true) {
        // Read victron battery.

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
