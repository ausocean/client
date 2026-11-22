/*
  Name:
    battery.cpp - Battery management for CuttleOS.

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

#include "include/battery.hpp"

#include <optional>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/adc_types.h"

#include "include/wifi.hpp"

// Logging tag.
static constexpr auto TAG = "Battery";

static adc_oneshot_unit_handle_t battery_handle;

esp_err_t init_battery()
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    auto err = adc_oneshot_new_unit(&init_config1, &battery_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to get new adc oneshot handle for battery: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(battery_handle, ADC_CHANNEL_0, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to configure channel: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

std::optional<long long> read_battery()
{
    auto err = wifi_off();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to turn wifi off: %s", esp_err_to_name(err));
        return std::nullopt;
    }

    int raw;
    err = adc_oneshot_read(battery_handle, ADC_CHANNEL_0, &raw);

    err = wifi_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to turn wifi on: %s", esp_err_to_name(err));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to read ADC channel 0: %s", esp_err_to_name(err));
        return std::nullopt;
    }

    ESP_LOGI(TAG, "got battery reading: %d", raw);

    return raw;
}
