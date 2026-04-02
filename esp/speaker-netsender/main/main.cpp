/*
  Name:
    main.cpp - Speaker NetSender for an ESP32 powered Speaker.

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

// Make the app c++ compatible.
#include <string>
#include <string_view>
extern "C" {
    void app_main();
}

#include <stdio.h>
#include <string.h>
#include <array>
#include <functional>
#include <string>

#include "freertos/projdefs.h"
#include "netsender.hpp"
#include "include/netsender_vars.hpp"
#include "include/audio.hpp"
#include "include/ethernet.hpp"
#include "include/sd.hpp"
#include "include/globals.h"
#include "include/log.hpp"
#include "soc/clk_tree_defs.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "esp_log.h"
#include "tas5805.hpp"
#include "esp_log_color.h"

// Current version of the speaker.
static constexpr const auto SPEAKER_VERSION = "0.4.2";

// File to save variables to.
static const constexpr auto VARS_FILE = "variables.txt";

// Tag used in logs.
static constexpr const auto TAG = "speaker";

// Netsender Instance.
static Netsender ns;

// Device variables.
netsender::device_var_state_t vars;

// Handle for the audio player task.
static TaskHandle_t player_handle;

// Callback function to be registered with NetSender to parse vars response.
// TODO: Consider if this should also be an auto-generated function.
esp_err_t parse_vars(std::string var_resp)
{
    ESP_LOGD(TAG, "parsing variables in callback");

    // Get the ID in the response.
    std::string id;
    auto has_id = netsender_extract_json(var_resp, "id", id);
    if (!has_id) {
        ESP_LOGE(TAG, "unable to get ID from var response");
        return ESP_FAIL;
    }

    // Parse the registered variables into the vars struct.
    std::string val;
    std::string var_name;
    bool has_val;
    for (auto i = 0; i < netsender::VAR_COUNT; i++) {
        if (strcmp(netsender::VARIABLES[i], netsender::var::VAR_ID_VS) == 0) {
            // Varsum (vs) is not prepended with id scope.
            var_name = netsender::VARIABLES[i];
        } else {
            var_name = id + "." + netsender::VARIABLES[i];
        }
        ESP_LOGI(TAG, "looking for variable: %s", var_name.c_str());
        has_val = netsender_extract_json(var_resp, var_name.c_str(), val);
        if (has_val) {
            netsender::update_state_member(vars, netsender::VARIABLES[i], val);
            ESP_LOGI(TAG, "got variable: %s=%s", var_name.c_str(), val.c_str());
        }
    }

    // Save the variables to the SD card.
    constexpr const auto MAX_VAR_PATH_LEN = 64;
    char var_file[MAX_VAR_PATH_LEN];
    snprintf(var_file, MAX_VAR_PATH_LEN, "%s/%s", MOUNT_POINT, VARS_FILE);

    auto err = netsender::write_vars_to_file(vars, var_file);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to write the vars to file");
        // We don't return an error as this shouldn't affect runtime flow.
    } else {
        ESP_LOGI(TAG, "wrote vars to file");
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initialising ethernet");
    init_ethernet();
    ESP_LOGI(TAG, "Ethernet initialised");

    ESP_LOGI(TAG, "Initialising UDP Logging");
    init_udp_logging();
    ESP_LOGI(TAG, "Logging Initialised");

    ESP_LOGI(TAG, "Initialising SD card");
    init_sd();
    ESP_LOGI(TAG, "SD initialised");

    ESP_LOGI(TAG, "Initialising I2S Amp");
    auto amp = init_amp();
    ESP_LOGI(TAG, "Amp Initialised");

    ESP_LOGI(TAG, "Speaker Netsender Version: %s", SPEAKER_VERSION);

    // Get any stored variables from the SD card when it exists.
    constexpr const auto MAX_VAR_PATH_LEN = 64;
    char var_file[MAX_VAR_PATH_LEN];
    snprintf(var_file, MAX_VAR_PATH_LEN, "%s/%s", MOUNT_POINT, VARS_FILE);
    auto got_vars = netsender::read_vars_from_file(var_file);
    if (!got_vars.has_value()) {
        ESP_LOGW(TAG, "got no variables from SD");
    } else {
        ESP_LOGI(TAG, "got variables from SD");
        vars = got_vars.value();
        ns.set_varsum(vars.vs);
    }

    // Start the Audio Task.
    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, &amp, 5, &player_handle, 1);

    // Register callback function to parse variables.
    ns.register_variable_parser(parse_vars);

    // Start the netsender task.
    ns.start();

    char cur_audio_file[netsender::MAX_STR_VAR_LEN];
    strlcpy(cur_audio_file, vars.FilePath, sizeof(cur_audio_file));

    while (true) {
        amp.set_volume(vars.Volume);

        if (strcmp(cur_audio_file, vars.FilePath) != 0) {
            ESP_LOGI(TAG, "audio file variable has changed, loading new audio");
            reload_requested = true;
            auto err = download_file_to_sdcard();
            if (err == ESP_OK) {
                reload_requested = false;
            } else {
                ESP_LOGE(TAG, "couldn't load new file, continuing with old file");
            }
            strncpy(cur_audio_file, vars.FilePath, sizeof(cur_audio_file));
            amp.pause();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
