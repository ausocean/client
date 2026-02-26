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

#include "tas5805.hpp"
#include "driver/i2c_master.h"
#include "driver/i2s_common.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <cstdlib>
#include <esp_log.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "tas5805";

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

TAS5805::TAS5805(i2c_master_bus_handle_t i2c_bus_handle,
                 i2s_chan_handle_t *i2s_tx_handle)
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
    uint8_t cmd = 0x00;
    write_reg(TAS8505_CHANGE_PAGE_REG, &cmd); // Go to page 0.
    write_reg(TAS8505_CHANGE_BOOK_REG, &cmd); // Go to book 0.

    // Set device to Hi-Z state before configuration.
    cmd = 0x02;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd);

    /* Set device settings (1) - offset: 02h
        Bits:
          7:    0   - Reserved
          6-4:  000 - 768K (FSW_SEL)
          3:    0   - Reserved
          2:    1   - PBTL Mode (DAMP_PBTL)
          1-0:  00  - BD Modulation (DAMP_MOD)
          = 0b0000 0100 = 0x04
    */
    cmd = 0x04;
    write_reg(TAS8505_DEVICE_CTRL_1_REG, &cmd);

    /* Set device analog gain - offset: 54h
        Bits:
          7-5:  000   - Reserved
          4-0:  00000 - 0dB (Max Vol) (ANA_GAIN)
          = 0b0000 0000 = 0x00
    */
    cmd = 0x00;
    write_reg(TAS8505_AGAIN_REG, &cmd);

    // Set device digital volume.
    this->set_volume(80);

    /* Set device settings (2) - offset: 03h
        Bits:
          7-5:  000 - Reserved
          4:    0   - Don't reset DSP (DIS_DSP)
          3:    0   - Normal Volume (MUTE)
          2:    0   - Reserved
          1-0:  11  - Play (CTRL_STATE)
          = 0b0000 0011 = 0x03
    */
    cmd = 0x03;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd);

    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t TAS5805::play(const char *path, volatile bool* kill_request)
{
    // Ensure a valid I2S handle exists to send audio.
    if (tx_handle == NULL || *tx_handle == NULL) {
        ESP_LOGE(TAG, "tx_handle must be not-NULL to play audio");
        return ESP_FAIL;
    }

    // Put the device into play state.
    uint8_t cmd = 0x00;
    write_reg(TAS8505_CHANGE_PAGE_REG, &cmd); // Go to page 0.
    write_reg(TAS8505_CHANGE_BOOK_REG, &cmd); // Go to book 0.
    cmd = 0x03;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Could not open file: %s", path);
        return ESP_FAIL;
    }
    // Create a 32KB staging buffer in internal DMA RAM, this improves SD
    // read/write performance.
    size_t fs_buf_size = 32768;
    char *fs_buf = (char *)heap_caps_malloc(fs_buf_size, MALLOC_CAP_DMA);

    // Set the audio file to use full buffering (_IOFBF), with the buffer
    // that was just created.
    if (fs_buf) {
        setvbuf(f, fs_buf, _IOFBF, fs_buf_size);
    }

    // Read WAV header.
    wav_header_t header;
    if (fread(&header, sizeof(wav_header_t), 1, f) != 1) {
        fclose(f);
        free(fs_buf);
        return ESP_FAIL;
    }

    // Use 4096 samples as our working chunk.
    const size_t samples_per_read = 4096;
    size_t frame_size = sizeof(int16_t) * header.num_channels;

    // Buffer for reading from file.
    int16_t *file_buf = (int16_t *)heap_caps_malloc(samples_per_read * frame_size,
                                                    MALLOC_CAP_8BIT);
    // Buffer for writing to I2S.
    // NOTE: The I2S buffer will always be stereo, regardless of the file type.
    int16_t *i2s_buf = (int16_t *)heap_caps_malloc(
                           samples_per_read * 2 * sizeof(int16_t), MALLOC_CAP_DMA);

    // Verify that buffers got allocated successfully.
    if (!file_buf || !i2s_buf) {
        if (file_buf) {
            free(file_buf);
        }
        if (i2s_buf) {
            free(i2s_buf);
        }

        fclose(f);
        free(fs_buf);
        return ESP_FAIL;
    }

    size_t samples_read;
    size_t bytes_written;

    while (
        (samples_read = fread(file_buf, frame_size, samples_per_read, f)) > 0
    ) {
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
            memcpy(i2s_buf, file_buf, samples_read * 2 * sizeof(int16_t));
        }

        // Write audio to the amplifier.
        i2s_channel_write(*tx_handle, i2s_buf, samples_read * 2 * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);

        if (kill_request && *kill_request) {
            ESP_LOGI("AUDIO", "Kill request received, stopping playback.");
            break;
        }
    }

    // Push final silence to clear the amp's pipeline.
    memset(i2s_buf, 0, 512 * 4);
    i2s_channel_write(*tx_handle, i2s_buf, 512 * 4, &bytes_written,
                      pdMS_TO_TICKS(100));

    free(file_buf);
    free(i2s_buf);

    fclose(f);
    if (fs_buf) {
        free(fs_buf);
    }

    return ESP_OK;
}

esp_err_t TAS5805::pause()
{
    uint8_t cmd = 0x00;
    write_reg(TAS8505_CHANGE_PAGE_REG, &cmd); // Go to page 0.
    write_reg(TAS8505_CHANGE_BOOK_REG, &cmd); // Go to book 0.


    // Set device to sleep.
    cmd = 0x01;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd);

    return ESP_OK;
}

esp_err_t TAS5805::set_volume(uint8_t vol)
{
    uint8_t cmd = 0x00;
    write_reg(TAS8505_CHANGE_PAGE_REG, &cmd); // Go to page 0.
    write_reg(TAS8505_CHANGE_BOOK_REG, &cmd); // Go to book 0.

    // Bound volume.
    if (vol < 0) {
        vol = 0;
    } else if (vol > 100) {
        vol = 100;
    }

    // Scale and invert volume from 0-100 to 255-0.
    // TODO: Make this a more linear sounding scale.
    cmd = 255 - ((uint16_t)vol * 255 / 100);

    // Write the new volume set point.
    write_reg(TAS8505_DIG_VOL_CTRL_REG, &cmd);

    return ESP_OK;
}

void TAS5805::write_reg(const int reg, const uint8_t *cmd)
{
    ESP_LOGD(TAG, "Writing to Register: 0x%02X", reg);

    // Put the register first, and then the data.
    uint8_t data[] = {static_cast<uint8_t>(reg), *cmd};

    // Transmit the command.
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, 2, 50));
}

TAS5805::~TAS5805()
{
    delete tx_handle;
}
