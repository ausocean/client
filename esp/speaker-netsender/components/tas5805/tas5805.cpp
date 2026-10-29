/*
  Name:
    tas5805.cpp - An ESP-IDF component to support the TAS5805 Amplifier module.

  Description:
    See https://www.cloudblue.org

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

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_log_color.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/i2c_types.h"

#include "portmacro.h"
#include "register_cmds.hpp"
#include "sdkconfig.h"
#include "tas5805.hpp"

static const char *TAG = "tas5805";

static constexpr const auto DEFAULT_VOLUME = 80;

typedef struct {
    char chunk_id[4]; // "RIFF"
    uint32_t chunk_size;
    char format[4];       // "WAVE"
    char subchunk1_id[4]; // "fmt "
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char subchunk2_id[4]; // "data"
    uint32_t subchunk2_size;
} wav_header_t;

TAS5805::TAS5805(i2c_master_bus_handle_t i2c_bus_handle, i2s_chan_handle_t i2s_tx_handle)
    : bus_handle(i2c_bus_handle), tx_handle(i2s_tx_handle)
{
    // Initialise I2C device.
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = static_cast<uint16_t>(CONFIG_AMP_I2C_ADDRESS),
        .scl_speed_hz = CONFIG_AMP_I2C_CLOCK_SPEED,
    };

    // Add the device to the bus.
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // Configure default settings for the amplifier.
    write_reg(TAS5805_CHANGE_PAGE_REG, PAGE::ZERO); // Go to page 0.
    write_reg(TAS5805_CHANGE_BOOK_REG, BOOK::ZERO); // Go to book 0.

    // Set device to Hi-Z state before configuration.
    write_reg(TAS5805_DEVICE_CTRL_2_REG, CTRL_STATE::HI_Z);

    write_reg(TAS5805_DEVICE_CTRL_1_REG, DAMP_PBTL::PBTL_MODE); // Set PBTL MODE.
    write_reg(TAS5805_AGAIN_REG, ANA_GAIN::DB_0);               // Set Analog gain = 0dB.

    // Set device digital volume.
    this->set_volume(DEFAULT_VOLUME);

    // Set back to play mode.
    write_reg(TAS5805_DEVICE_CTRL_2_REG, CTRL_STATE::PLAY);

    vTaskDelay(pdMS_TO_TICKS(10));
}

bool TAS5805::is_connected()
{
    write_reg(TAS5805_CHANGE_PAGE_REG, PAGE::ZERO); // Go to page 0.
    write_reg(TAS5805_CHANGE_BOOK_REG, BOOK::ZERO); // Go to book 0.

    uint8_t reg = TAS5805_CLKDET_STATUS_REG;
    uint8_t data_out = 0;

    esp_err_t err = i2c_master_transmit_receive(dev_handle, &reg, 1, &data_out, 1, 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C Read Failed: %s", esp_err_to_name(err));
        return false;
    }

    constexpr uint8_t PLL_LOCK = 0x08;
    if ((data_out & PLL_LOCK) != PLL_LOCK) {
        ESP_LOGE(TAG, "PLL not locked");
        return false;
    }

    return true;
}

esp_err_t TAS5805::prepare_buffers()
{
    if (fs_buf != nullptr && file_buf != nullptr && i2s_buf != nullptr) {
        return ESP_OK;
    }

    // The stdio buffer is not used directly by a DMA engine, so it can live
    // in normal internal RAM.
    if (fs_buf == nullptr) {
        fs_buf = static_cast<char *>(heap_caps_malloc(FS_BUF_SIZE, MALLOC_CAP_8BIT));
    }

    if (file_buf == nullptr) {
        file_buf = static_cast<int16_t *>(
            heap_caps_malloc(SAMPLES_PER_READ * MAX_CHANNELS * sizeof(int16_t), MALLOC_CAP_8BIT));
    }

    // The I2S descriptor and buffers must be DMA capable.
    if (i2s_buf == nullptr) {
        i2s_buf =
            static_cast<int16_t *>(heap_caps_malloc(SAMPLES_PER_READ * MAX_CHANNELS * sizeof(int16_t), MALLOC_CAP_DMA));
    }

    if (fs_buf == nullptr || file_buf == nullptr || i2s_buf == nullptr) {
        ESP_LOGE(TAG, "unable to allocate playback buffers");
        free_buffers();
        return ESP_FAIL;
    }

    return ESP_OK;
}

void TAS5805::free_buffers()
{
    free(fs_buf);
    free(file_buf);
    free(i2s_buf);
    fs_buf = nullptr;
    file_buf = nullptr;
    i2s_buf = nullptr;
}

esp_err_t TAS5805::play(const char *path, volatile bool *kill_request)
{
    // Ensure a valid I2S handle exists to send audio.
    if (tx_handle == NULL) {
        ESP_LOGE(TAG, "tx_handle must be not-NULL to play audio");
        return ESP_FAIL;
    }

    // Allocate the buffers on the first playback pass only.
    if (prepare_buffers() != ESP_OK) {
        return ESP_FAIL;
    }

    // Put the device into play state.
    write_reg(TAS5805_CHANGE_PAGE_REG, PAGE::ZERO); // Go to page 0.
    write_reg(TAS5805_CHANGE_BOOK_REG, BOOK::ZERO); // Go to book 0.
    write_reg(TAS5805_DEVICE_CTRL_2_REG, CTRL_STATE::PLAY);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Could not open file: %s", path);
        return ESP_FAIL;
    }

    // Set the audio file to use full buffering (_IOFBF), with the reusable
    // staging buffer.
    setvbuf(f, fs_buf, _IOFBF, FS_BUF_SIZE);

    // Read WAV header.
    wav_header_t header;
    if (fread(&header, sizeof(wav_header_t), 1, f) != 1) {
        fclose(f);
        return ESP_FAIL;
    }

    // Only 16-bit mono or stereo PCM is supported by the I2S configuration.
    if (header.num_channels < 1 || header.num_channels > MAX_CHANNELS || header.bits_per_sample != 16) {
        ESP_LOGE(TAG, "unsupported WAV format: %u channels, %u bits per sample", header.num_channels,
                 header.bits_per_sample);
        fclose(f);
        return ESP_FAIL;
    }

    size_t frame_size = sizeof(int16_t) * header.num_channels;

    size_t samples_read;
    size_t bytes_written;

    while ((samples_read = fread(file_buf, frame_size, SAMPLES_PER_READ, f)) > 0) {
        if (header.num_channels == 1) {
            // We need to fill every second sample (right-channel), with a
            // copy of the left channel audio, since the original file is mono,
            // and the output must be stereo.
            for (size_t i = 0; i < samples_read; i++) {
                int16_t sample = file_buf[i];
                i2s_buf[i * 2] = sample;
                i2s_buf[i * 2 + 1] = sample;
            }
        } else {
            // Just copy the file buffer directly to the I2S buffer.
            memcpy(i2s_buf, file_buf, samples_read * frame_size);
        }

        // Write audio to the amplifier.
        i2s_channel_write(tx_handle, i2s_buf, samples_read * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);

        if (kill_request && *kill_request) {
            ESP_LOGI("AUDIO", "Kill request received, stopping playback.");
            break;
        }
    }

    // Push final silence to clear the amp's pipeline.
    memset(i2s_buf, 0, 512 * 4);
    i2s_channel_write(tx_handle, i2s_buf, 512 * 4, &bytes_written, pdMS_TO_TICKS(100));

    fclose(f);
    return ESP_OK;
}

esp_err_t TAS5805::pause()
{
    write_reg(TAS5805_CHANGE_PAGE_REG, PAGE::ZERO); // Go to page 0.
    write_reg(TAS5805_CHANGE_BOOK_REG, BOOK::ZERO); // Go to book 0.

    // Set device to sleep.
    write_reg(TAS5805_DEVICE_CTRL_2_REG, CTRL_STATE::SLEEP);

    return ESP_OK;
}

esp_err_t TAS5805::set_volume(uint8_t vol)
{
    write_reg(TAS5805_CHANGE_PAGE_REG, PAGE::ZERO); // Go to page 0.
    write_reg(TAS5805_CHANGE_BOOK_REG, BOOK::ZERO); // Go to book 0.

    // Bound volume.
    if (vol > 100) {
        vol = 100;
    }

    // The digital volume register is 0x00 = +24 dB, 0x30 = 0 dB, with 0.5 dB
    // steps down to 0xFF = -103.5 dB. A linear register mapping makes the
    // lower half of the range almost inaudible while the top end is very loud.
    // Use a square-law amplitude curve (40 * log10(vol / 100) dB), which gives
    // a much more natural sounding taper, and cap the output at 0 dB.
    uint8_t cmd = 0xFF; // Mute.
    if (vol > 0) {
        const auto db = 40.0f * std::log10(static_cast<float>(vol) / 100.0f);
        auto reg = std::lround(48.0f - 2.0f * db); // 48 == 0 dB.
        if (reg < 48) {
            reg = 48;
        } else if (reg > 0xFF) {
            reg = 0xFF;
        }
        cmd = static_cast<uint8_t>(reg);
    }

    // Write the new volume set point.
    write_reg(TAS5805_DIG_VOL_CTRL_REG, cmd);

    return ESP_OK;
}

void TAS5805::write_reg(const int reg, const uint8_t cmd)
{
    ESP_LOGD(TAG, "Writing to Register: 0x%02X", reg);

    // Put the register first, and then the data.
    uint8_t data[] = {static_cast<uint8_t>(reg), cmd};

    // Transmit the command.
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, 2, 50));
}

template <typename... Cmd>
    requires(std::is_enum_v<Cmd> && ...) && ((sizeof(Cmd) == 1) && ...) && (sizeof...(Cmd) > 0)
void TAS5805::write_reg(const int reg, const Cmd... cmds)
{
    const uint8_t cmd = (static_cast<uint8_t>(cmds) | ...);
    write_reg(reg, cmd);
}

TAS5805::~TAS5805()
{
    free_buffers();

    if (tx_handle != nullptr) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = nullptr;
    }
    i2c_master_bus_rm_device(dev_handle);
}
