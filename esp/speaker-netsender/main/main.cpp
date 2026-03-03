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
extern "C" {
    void app_main();
}

#include "driver/i2s_types.h"
#include "freertos/projdefs.h"
#include "netsender.hpp"
#include "include/netsender_vars.hpp"
#include "include/audio.hpp"
#include "include/ethernet.hpp"
#include "soc/clk_tree_defs.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "netsender.hpp"
#include "soc/clk_tree_defs.h"
#include "driver/i2s_types.h"
#include "soc/clk_tree_defs.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include <ethernet_init.h>
#include <esp_eth.h>
#include <esp_netif.h>
#include <esp_types.h>
#include "esp_event.h"
#include <sdkconfig.h>
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "tas5805.hpp"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"

constexpr const char* SPEAKER_VERSION = "0.3.0";

// Mount point for the SD card filesystem.
const char* MOUNT_POINT = "/sdcard";

// Tag used in logs.
static constexpr const char* TAG = "speaker";

// Netsender Instance.
static Netsender ns;

// Device variables.
netsender::device_var_state_t vars;

// Handle for the audio player task.
static TaskHandle_t player_handle;

// Atomic flag for stopping audio playback.
// TODO: Use a better threadsafe option.
volatile bool reload_requested = false;

static auto init_sd()
{
    sdmmc_card_t *card;

    // Use the default host.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Configure the SPI bus to use the config values.
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = (gpio_num_t)CONFIG_SD_MOSI;
    bus_cfg.miso_io_num = (gpio_num_t)CONFIG_SD_MISO;
    bus_cfg.sclk_io_num = (gpio_num_t)CONFIG_SD_CLK;
    bus_cfg.quadwp_io_num = CONFIG_SD_QUADWP;
    bus_cfg.quadhd_io_num = CONFIG_SD_QUADHD;
    bus_cfg.max_transfer_sz = CONFIG_SD_MAX_TRANSFER_SZ;

    // Initialise the SPI Bus.
    ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    // Configure the SD Slot.
    sdspi_dev_handle_t sd_handle;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CS);
    slot_config.gpio_cd = static_cast<gpio_num_t>(CONFIG_SD_DET);
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    // Initialise the device.
    ESP_ERROR_CHECK(sdspi_host_init_device(&slot_config, &sd_handle));

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 32 * 1024;

    // Mount the filesystem.
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card));

    return card;
}

static TAS5805 init_amp()
{
    // Period of bus glitch to ignore (default suggestion).
    constexpr const int glitch_cnt = 7;

    // Configure and setup I2C.
    i2c_master_bus_config_t i2c_config = {};
    i2c_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_AMP_I2C_SDA);
    i2c_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_AMP_I2C_SCL);
    i2c_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_config.glitch_ignore_cnt = glitch_cnt;
    i2c_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &bus_handle));
    ESP_LOGI(TAG, "I2C Master bus created");

    // Configure I2S Channel.
    i2s_chan_handle_t* tx_handle = new i2s_chan_handle_t();
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 16;
    chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = CONFIG_AMP_I2S_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_APLL,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(CONFIG_AMP_I2S_BCLK),
            .ws   = static_cast<gpio_num_t>(CONFIG_AMP_I2S_WS),
            .dout = static_cast<gpio_num_t>(CONFIG_AMP_I2S_DOUT),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Initialise channel.
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_handle, &std_cfg));

    // Enable the channel.
    // NOTE: This MUST be done before initialising amp, as the amp requires a
    // stable clock before configuration.
    ESP_ERROR_CHECK(i2s_channel_enable(*tx_handle));
    ESP_LOGI(TAG, "I2S initialized and clocks started");

    // Create a new amplifier.
    return TAS5805(bus_handle, tx_handle);
}

// Callback function to be registered with NetSender to parse vars response.
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
    bool has_val;
    for (auto i = 0; i < netsender::VAR_COUNT; i++) {
        std::string var_name = id + "." + netsender::VARIABLES[i];
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

// Audio loop to be run in FreeRTOS task.
void audio_task(void *pvParameters)
{
    constexpr const auto AUDIO_TAG = "AUDIO";
    auto *amp = static_cast<TAS5805*>(pvParameters);
    if (amp == nullptr) {
        ESP_LOGE(AUDIO_TAG, "Received null pointer!");
        vTaskDelete(NULL);
        return;
    }

    // Start by pausing the audio to ensure the I2S buffer is empty
    amp->pause();

    // This will always be the same length.
    char filename[HASH_FILENAME_LEN];
    char file_path[netsender::MAX_STR_VAR_LEN];

    while (true) {
        url_to_filename(vars.FilePath, filename);
        snprintf(file_path, sizeof(file_path), "%s/%s", MOUNT_POINT, filename);
        while (reload_requested) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(AUDIO_TAG, "waiting for reload to complete");
        }
        ESP_LOGI(AUDIO_TAG, "Starting playback...");

        // This will block until the file ends, or reload requested is set to true.
        auto err = amp->play(file_path, &reload_requested);
        if (err != ESP_OK) {
            ESP_LOGE(AUDIO_TAG, "Playback error, retrying in 1s...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Speaker Netsender Version: %s", SPEAKER_VERSION);

    ESP_LOGI(TAG, "Initialising ethernet");
    init_ethernet();
    ESP_LOGI(TAG, "Ethernet initialised");

    ESP_LOGI(TAG, "Initialising SD card");
    auto sd_card = init_sd();
    ESP_LOGI(TAG, "SD initialised");

    ESP_LOGI(TAG, "Initialising I2S Amp");
    auto amp = init_amp();
    ESP_LOGI(TAG, "Amp Initialised");

    // Start the Audio Task.
    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, &amp, 5, &player_handle, 1);

    // Register callback function to parse variables.
    ns.register_variable_parser(parse_vars);

    // Start the netsender task.
    ns.start();

    char cur_audio_file[netsender::MAX_STR_VAR_LEN];
    strncpy(cur_audio_file, vars.FilePath, 64);

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
            strncpy(cur_audio_file, vars.FilePath, strlen(vars.FilePath));
            amp.pause();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
