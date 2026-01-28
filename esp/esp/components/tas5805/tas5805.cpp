#include "tas5805.hpp"
#include "driver/i2c_master.h"
#include "driver/i2s_common.h"
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

    /* Set device digital volume - offset: 4Ch
        Bits:
          7-0:  00100000   - ~30% Volume
          = 0b0010 0000 = 0x20
    */
    cmd = 0x30;
    write_reg(TAS8505_DIG_VOL_CTRL_REG, &cmd);

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

void TAS5805::play(const char *path)
{
    // Ensure a valid I2S handle exists to send audio.
    if (tx_handle == NULL || *tx_handle == NULL) {
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Could not open file: %s", path);
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

    internal_play_loop(f);

    fclose(f);
    if (fs_buf) {
        free(fs_buf);
    }

}

void TAS5805::internal_play_loop(FILE *f)
{

    wav_header_t header;
    if (fread(&header, sizeof(wav_header_t), 1, f) != 1) {
        return;
    }

    // Use 4096 samples (16KB total for stereo 16-bit) as our working chunk
    const size_t samples_per_read = 4096;
    size_t frame_size = sizeof(int16_t) * header.num_channels;

    // Use internal RAM for file_buf instead of SPIRAM to ensure max speed
    int16_t *file_buf = (int16_t *)heap_caps_malloc(samples_per_read * frame_size,
                                                    MALLOC_CAP_8BIT);
    int16_t *i2s_buf = (int16_t *)heap_caps_malloc(
                           samples_per_read * 2 * sizeof(int16_t), MALLOC_CAP_DMA);

    if (!file_buf || !i2s_buf) {
        if (file_buf) {
            free(file_buf);
        }
        if (i2s_buf) {
            free(i2s_buf);
        }
        return;
    }

    size_t samples_read;
    size_t bytes_written;

    while ((samples_read = fread(file_buf, frame_size, samples_per_read, f)) >
            0) {
        // MONO -> STEREO Conversion
        if (header.num_channels == 1) {
            for (size_t i = 0; i < samples_read; i++) {
                int16_t sample = file_buf[i];
                i2s_buf[i * 2] = sample;
                i2s_buf[i * 2 + 1] = sample;
            }
        }
        // STEREO -> STEREO
        else {
            memcpy(i2s_buf, file_buf, samples_read * 2 * sizeof(int16_t));
        }

        // Blocking call: This handles the Watchdog feeding automatically
        i2s_channel_write(*tx_handle, i2s_buf, samples_read * 2 * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);
    }

    // Push final silence to clear the amp's pipeline
    memset(i2s_buf, 0, 512 * 4);
    i2s_channel_write(*tx_handle, i2s_buf, 512 * 4, &bytes_written,
                      pdMS_TO_TICKS(100));

    free(file_buf);
    free(i2s_buf);
}

/**
 * @param sample_index The current sample number (increments forever)
 * @param target_freq  The frequency you want to hear (e.g., 440.0 for A4)
 * @param sample_rate  Your I2S sample rate (e.g., 44100)
 * @param amplitude    How loud (0 to 32767). 10000-20000 is usually safe.
 */
int16_t calculate_sine(size_t sample_index, float target_freq, int sample_rate,
                       int16_t amplitude)
{
    // Standard formula: y = A * sin(2 * PI * f * t)
    // where t = sample_index / sample_rate
    float time = (float)sample_index / (float)sample_rate;
    float angle = 2.0f * (float)M_PI * target_freq * time;

    return (int16_t)(amplitude * sinf(angle));
}
void TAS5805::play_beep(uint32_t duration_ms)
{
    const int sample_rate = CONFIG_AMP_I2S_SAMPLE_RATE;
    const float frequency = 440.0f;
    const int16_t amplitude = 300;

    const size_t samples_per_cycle = sample_rate / frequency;
    int16_t *lut = (int16_t *)malloc(samples_per_cycle * sizeof(int16_t));
    if (!lut) {
        return;
    }

    for (size_t i = 0; i < samples_per_cycle; i++) {
        float angle = (2.0f * M_PI * i) / samples_per_cycle;
        lut[i] = (int16_t)(sinf(angle) * amplitude);
    }

    const size_t chunk_samples = 512;
    const size_t buffer_size = chunk_samples * 2 * sizeof(int16_t);
    int16_t *dma_buffer =
        (int16_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);

    if (dma_buffer) {
        size_t total_samples_to_play = (sample_rate * duration_ms) / 1000;
        size_t samples_played = 0;
        size_t lut_index = 0;
        size_t bytes_written; // Moved scope outside the loops

        while (samples_played < total_samples_to_play) {
            for (size_t i = 0; i < chunk_samples; i++) {
                int16_t val = lut[lut_index];
                dma_buffer[i * 2] = val;
                dma_buffer[i * 2 + 1] = val;
                lut_index = (lut_index + 1) % samples_per_cycle;
            }

            i2s_channel_write(*tx_handle, dma_buffer, buffer_size, &bytes_written,
                              portMAX_DELAY);
            samples_played += chunk_samples;
        }

        // Clean up: Push silence to clear the DMA buffer
        memset(dma_buffer, 0, buffer_size);
        i2s_channel_write(*tx_handle, dma_buffer, buffer_size, &bytes_written,
                          portMAX_DELAY);

        free(dma_buffer);
    }

    free(lut);
    // Be careful with disable here; if you call play_beep frequently,
    // maybe leave it enabled or move this to a dedicated stop function.
    i2s_channel_disable(*tx_handle);
}

void TAS5805::test_performance_gap()
{
    const size_t test_samples = 1024;
    int16_t *ram_buffer = (int16_t *)heap_caps_malloc(
                              test_samples * 2 * sizeof(int16_t), MALLOC_CAP_DMA);

    // Fill with a simple tone once
    for (int i = 0; i < test_samples; i++) {
        int16_t val = (int16_t)(10000 * sin(2 * M_PI * 440.0 * i / 44100.0));
        ram_buffer[i * 2] = val;
        ram_buffer[i * 2 + 1] = val;
    }

    // Loop 1000 times (about 23 seconds of audio)
    for (int i = 0; i < 1000; i++) {
        size_t written;
        // Writing from RAM is near-instant.
        // If this stutters, your I2S clock/GPIO config is the culprit.
        i2s_channel_write(*tx_handle, ram_buffer,
                          test_samples * 2 * sizeof(int16_t), &written,
                          portMAX_DELAY);
    }

    // --- THE STOP SEQUENCE ---
    // 1. Fill buffer with zeros (silence)
    memset(ram_buffer, 0, test_samples * 2 * sizeof(int16_t));
    size_t written;
    // 2. Write silence to push the actual audio out of the DMA tail
    i2s_channel_write(*tx_handle, ram_buffer, test_samples * 2 * sizeof(int16_t),
                      &written, portMAX_DELAY);
}

void TAS5805::write_reg(const int reg, const uint8_t *cmd)
{
    ESP_LOGD(TAG, "Writing to Register: 0x%02X", reg);
    ESP_LOG_BUFFER_HEX(TAG, cmd, 1);

    // Put the register first, and then the data.
    uint8_t data[] = {static_cast<uint8_t>(reg), *cmd};

    // Transmit the command.
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, 2, 50));
}

TAS5805::~TAS5805()
{
    delete tx_handle;
}
