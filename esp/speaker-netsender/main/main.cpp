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

// This must be included before other freertos includes.
#include "freertos/FreeRTOS.h" // IWYU pragma: keep

#include <cstdio>
#include <cstring>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

#include "include/audio.hpp"
#include "include/ethernet.hpp"
#include "include/globals.h"
#include "include/netsender_vars.hpp"
#include "include/sd.hpp"
#include "include/self_check.hpp"
#include "include/time.hpp"
#include "log.hpp"
#include "netsender.hpp"
#include "ota_tcp.hpp"
#include "tas5805.hpp"

// Make the app c++ compatible.
extern "C" {
void app_main();
}

// Current version of the speaker.
static constexpr const auto SPEAKER_VERSION = "0.5.0";

// File to save variables to.
static const constexpr auto VARS_FILE = "variables.txt";

// Tag used in logs.
static constexpr const auto TAG = "speaker";

// The audio task runs on core 1 and is the highest priority application task,
// so network and logging work cannot delay playback.
static constexpr const auto AUDIO_TASK_CORE = 1;
static constexpr const auto AUDIO_TASK_PRIORITY = 10;
static constexpr const auto AUDIO_TASK_STACK_SIZE = 6144;

// Netsender Instance.
static Netsender ns;

// Device variables.
netsender::device_var_state_t vars;
std::mutex vars_mutex;

// URL of the audio file that has been successfully downloaded and is safe to
// play. Protected by vars_mutex.
char current_audio_url[netsender::MAX_STR_VAR_LEN] = {0};

// Handle for the audio player task.
static TaskHandle_t player_handle;

// Callback function to be registered with NetSender to parse vars response.
// TODO: Consider if this should also be an auto-generated function.
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

    // Work on a local copy so the audio and main tasks can keep reading the
    // shared state while this callback is parsing.
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

    bool vars_changed = false;
    {
        std::lock_guard<std::mutex> lock(vars_mutex);
        // The varsum changes on every vars request, but it does not need to be
        // persisted on its own. Only persist actual configuration changes.
        auto previous = vars;
        previous.vs = updated.vs;
        vars_changed = memcmp(&previous, &updated, sizeof(updated)) != 0;
        vars = updated;
    }

    // Persisting to SD is deferred to the audio task between tracks so neither
    // the HTTP poll path nor active playback performs SD I/O for this.
    if (vars_changed) {
        vars_dirty = true;
    }

    return ESP_OK;
}

esp_err_t persist_vars_if_dirty()
{
    if (!vars_dirty.exchange(false)) {
        return ESP_OK;
    }

    netsender::device_var_state_t snapshot;
    {
        std::lock_guard<std::mutex> lock(vars_mutex);
        snapshot = vars;
    }

    constexpr const auto MAX_VAR_PATH_LEN = 64;
    char var_file[MAX_VAR_PATH_LEN];
    snprintf(var_file, MAX_VAR_PATH_LEN, "%s/%s", MOUNT_POINT, VARS_FILE);

    auto err = netsender::write_vars_to_file(snapshot, var_file);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to write the vars to file");
        vars_dirty = true;
        return err;
    }

    ESP_LOGD(TAG, "wrote vars to file");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initialising ethernet");
    init_ethernet();
    ESP_LOGI(TAG, "Ethernet initialised");

    ESP_LOGI(TAG, "Initialising SD card");
    init_sd();
    ESP_LOGI(TAG, "SD initialised");

    ESP_LOGI(TAG, "Initialising I2S Amp");
    auto amp = init_amp();
    ESP_LOGI(TAG, "Amp Initialised");

    // Perform partition self-check.
    const auto valid_partition = self_check_ok(&amp);
    ota_tcp_validate_partition(valid_partition);

    ESP_LOGI(TAG, "Initialising SNTP");
    initialise_sntp();
    ESP_LOGI(TAG, "SNTP Initialised");

    ESP_LOGI(TAG, "Initialising Logging");
    auto read_bin_func = init_logging();
    char log_pin_name[4] = "T0";
    ns.register_binary_input(log_pin_name, read_bin_func);
    ESP_LOGI(TAG, "Logging Initialised");

    ESP_LOGI(TAG, "Initialising OTA listener");
    init_ota_tcp();
    ESP_LOGI(TAG, "OTA Initialised");

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
        auto stored = got_vars.value();
        {
            std::lock_guard<std::mutex> lock(vars_mutex);
            vars = stored;
        }
        ns.set_varsum(stored.vs);
    }

    // Start the Audio Task.
    xTaskCreatePinnedToCore(audio_task, "audio_task", AUDIO_TASK_STACK_SIZE, &amp, AUDIO_TASK_PRIORITY, &player_handle,
                            AUDIO_TASK_CORE);

    // Register callback function to parse variables.
    ns.register_variable_parser(parse_vars);

    // Start the netsender task.
    ns.start();

    // Avoid hammering the network if a download keeps failing.
    static constexpr auto DOWNLOAD_RETRY_INTERVAL = pdMS_TO_TICKS(10000);
    TickType_t last_download_attempt = 0;
    uint8_t last_volume = 0xFF;

    while (true) {
        char requested_file[netsender::MAX_STR_VAR_LEN];
        char active_file[netsender::MAX_STR_VAR_LEN];
        uint8_t volume = 0;
        {
            std::lock_guard<std::mutex> lock(vars_mutex);
            volume = static_cast<uint8_t>(vars.Volume);
            strlcpy(requested_file, vars.FilePath, sizeof(requested_file));
            strlcpy(active_file, current_audio_url, sizeof(active_file));
        }

        if (volume != last_volume) {
            amp.set_volume(volume);
            last_volume = volume;
        }

        const bool file_changed = requested_file[0] != '\0' && strcmp(active_file, requested_file) != 0;
        const bool retry_ready =
            last_download_attempt == 0 || (xTaskGetTickCount() - last_download_attempt) >= DOWNLOAD_RETRY_INTERVAL;
        if (file_changed && retry_ready) {
            ESP_LOGI(TAG, "audio file variable has changed, loading new audio");
            last_download_attempt = xTaskGetTickCount();

            // Download the new file while the current file continues to play.
            // This keeps audio running if the download is slow or fails.
            auto err = download_file_to_sdcard(requested_file);
            if (err == ESP_OK) {
                // Mute the old file, publish the new URL, then request a
                // restart. The audio task clears reload_requested once it has
                // observed the request, so even a long SD stall cannot lose
                // the switch.
                amp.pause();
                {
                    std::lock_guard<std::mutex> lock(vars_mutex);
                    strlcpy(current_audio_url, requested_file, sizeof(current_audio_url));
                }
                reload_requested = true;
                last_download_attempt = 0;
            } else {
                ESP_LOGE(TAG, "couldn't load new file, continuing with old file");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
