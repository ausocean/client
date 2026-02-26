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
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "include/netsender_vars.hpp"
#include "include/globals.h"

static constexpr const auto TAG = "audio";

void url_to_filename(const char* url, char* out_filename)
{
    // Compute hash.
    unsigned char hash[32];
    mbedtls_sha256((const unsigned char*)url, strlen(url), hash, 0);

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
        auto ctx = static_cast<DownloadContext*>(evt->user_data);
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
    char filename[69];
    url_to_filename(vars.FilePath, filename);

    char file_path[128];
    snprintf(file_path, sizeof(file_path), "%s/%s", MOUNT_POINT, filename);

    struct stat buf;
    if (stat(file_path, &buf) == 0) {
        ESP_LOGI("DOWNLOAD", "using cached download");
        return ESP_OK;
    }

    // Open the file.
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

    ESP_LOGI("DOWNLOAD", "Status = %d, length = %lld",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));

    fclose(f);
    esp_http_client_cleanup(client);

    return ESP_OK;
}
