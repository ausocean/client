/*
  Name:
    audio.cpp - handling of audio file management.

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

#include "include/audio.hpp"

#include <cstring>
#include <stdio.h>
#include <sys/stat.h>

#include "driver/i2c_master.h" // IWYU pragma: keep
#include "driver/i2c_types.h"
#include "driver/i2s_common.h" // IWYU pragma: keep
#include "driver/i2s_std.h"    // IWYU pragma: keep
#include "driver/i2s_types.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/i2s_types.h"

#include "include/globals.h"
#include "include/netsender_vars.hpp"
#include "rom/sha.h"
#include "sdkconfig.h"
#include "sha/sha_core.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include "tas5805.hpp"

static constexpr const auto TAG = "audio";

void url_to_filename(const char *url, char *out_filename)
{
    unsigned char hash[32];
    esp_sha(SHA2_256, (const unsigned char *)url, strlen(url), hash);

    // Convert bytes to a hex string.
    for (int i = 0; i < 32; i++) {
        sprintf(&out_filename[i * 2], "%02x", hash[i]);
    }

    // Append filetype (".wav") and terminate.
    sprintf(&out_filename[64], ".wav");
    out_filename[68] = '\0';
}

struct DownloadContext {
    FILE *file;
    int total_downloaded;
    int last_logged_percent;
    int last_logged_bytes;
};

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA: {
        auto ctx = static_cast<DownloadContext *>(evt->user_data);
        auto bytes_written = fwrite(evt->data, 1, evt->data_len, ctx->file);
        ctx->total_downloaded = ctx->total_downloaded + bytes_written;
        if (ctx->total_downloaded >= ctx->last_logged_bytes + 100000) {
            ESP_LOGI("event", "downloaded %d bytes", ctx->total_downloaded);
            ctx->last_logged_bytes = ctx->total_downloaded;
        }
        break;
    }
    default:
        // Do nothing
        break;
    }
    return ESP_OK;
}

esp_err_t download_file_to_sdcard()
{
    char filename[HASH_FILENAME_LEN];
    url_to_filename(vars.FilePath, filename);

    char file_path[128];
    snprintf(file_path, sizeof(file_path), "%s/%s", MOUNT_POINT, filename);

    struct stat buf;
    if (stat(file_path, &buf) == 0) {
        ESP_LOGI("DOWNLOAD", "using cached download");
        return ESP_OK;
    }

    FILE *f = fopen(file_path, "wb");
    if (!f) {
        ESP_LOGE("DOWNLOAD", "Failed to open file");
        return ESP_FAIL;
    }

    const constexpr auto FILE_BUF_SIZE = 4 * 1024;
    static char file_buffer[FILE_BUF_SIZE];
    setvbuf(f, file_buffer, _IOFBF, FILE_BUF_SIZE);

    DownloadContext ctx = {
        .file = f,
        .total_downloaded = 0,
        .last_logged_percent = 0,
        .last_logged_bytes = 0,
    };

    // Configure the request.
    esp_http_client_config_t config = {};
    config.url = vars.FilePath;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.disable_auto_redirect = false;
    config.event_handler = _http_event_handler;
    config.user_data = &ctx;
    config.buffer_size = 4096;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    // Make the request.
    auto client = esp_http_client_init(&config);
    if (!client) {
        fclose(f);
        return ESP_FAIL;
    }
    auto err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE("DOWNLOAD", "Failed to perform download: %s", esp_err_to_name(err));
        fclose(f);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI("DOWNLOAD", "Status = %d, length = %lld", esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));

    fclose(f);
    esp_http_client_cleanup(client);

    return ESP_OK;
}

TAS5805 init_amp()
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
    i2s_chan_handle_t *tx_handle = new i2s_chan_handle_t();
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 16;
    chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz = CONFIG_AMP_I2S_SAMPLE_RATE,
                .clk_src = I2S_CLK_SRC_APLL,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(CONFIG_AMP_I2S_BCLK),
                .ws = static_cast<gpio_num_t>(CONFIG_AMP_I2S_WS),
                .dout = static_cast<gpio_num_t>(CONFIG_AMP_I2S_DOUT),
                .din = I2S_GPIO_UNUSED,
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

void audio_task(void *pvParameters)
{
    constexpr const auto AUDIO_TAG = "AUDIO";
    auto *amp = static_cast<TAS5805 *>(pvParameters);
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
