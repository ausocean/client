/*
  Name:
    tas5805.hpp - An ESP-IDF component to support the TAS5805 Amplifier module.

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

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "driver/i2c_types.h"
#include "driver/i2s_types.h"
#include "esp_err.h"

constexpr auto TAS5805_CHANGE_PAGE_REG = 0x00;
constexpr auto TAS5805_CHANGE_BOOK_REG = 0x7F;
constexpr auto TAS5805_DEVICE_CTRL_1_REG = 0x02;
constexpr auto TAS5805_DEVICE_CTRL_2_REG = 0x03;
constexpr auto TAS5805_CLKDET_STATUS_REG = 0x39;
constexpr auto TAS5805_DIG_VOL_CTRL_REG = 0x4C;
constexpr auto TAS5805_AGAIN_REG = 0x54;

/**
 * @brief TAS5805 is an I2S amplifier.
 */
class TAS5805 {
  public:
    /**
     * @brief Create a new TAS5805
     *
     * @param bus_handle i2c bus handler to connect the amplifier to.
     * @param tx_handle i2s transmit pipe handler to play audio.
     */
    TAS5805(i2c_master_bus_handle_t handle, i2s_chan_handle_t tx_handle);

    TAS5805(const TAS5805 &) = delete;
    TAS5805 &operator=(const TAS5805 &) = delete;

    /**
     * @brief returns the status of the device (communicating or not).
     */
    bool is_connected();

    /**
     * @brief Reads PCM data from a file and writes it to the I2S DMA buffer.
     * @param f Pointer to the opened audio file
     */
    esp_err_t play(const char *path, volatile bool *kill_request);

    /**
     * @brief Pauses playback of the amp.
     */
    esp_err_t pause();

    /**
     * @brief sets the digital volume
     * @param vol volume to set the speaker at (0 = mute, 100 = 0 dB).
     */
    esp_err_t set_volume(uint8_t vol);

    /**
     * @brief destructor.
     */
    ~TAS5805();

  private:
    /**
     * @brief Allocates the playback buffers on first use.
     */
    esp_err_t prepare_buffers();

    /**
     * @brief Releases the playback buffers.
     */
    void free_buffers();

    /**
     * @brief Writes the data to the given register.
     *
     * @param reg number of the register to be written.
     * @param data data to be written to the register.
     */
    void write_reg(const int reg, const uint8_t data);

    /**
     * @brief Write enum values to the given register.
     *
     * @param reg number of the register to be written.
     * @param cmds variadic list of uint8_t command enums.
     */
    template <typename... Cmds>
        requires(std::is_enum_v<Cmds> && ...) && ((sizeof(Cmds) == 1) && ...) && (sizeof...(Cmds) > 0)
    void write_reg(const int reg, const Cmds... cmds);

    /** I2C master handles */
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;

    /** I2S handle */
    i2s_chan_handle_t tx_handle = nullptr;

    /** Number of samples read from the file for each I2S write. */
    static constexpr size_t SAMPLES_PER_READ = 4096;

    /** Maximum number of channels supported in an audio file. */
    static constexpr size_t MAX_CHANNELS = 2;

    /** Size of the file stream buffer. */
    static constexpr size_t FS_BUF_SIZE = 32768;

    /**
     * @brief Playback buffers, allocated once on first playback and reused.
     *
     * fs_buf is used by stdio for file access, file_buf holds samples read
     * from the file, and i2s_buf is the DMA-capable buffer written to I2S.
     */
    char *fs_buf = nullptr;
    int16_t *file_buf = nullptr;
    int16_t *i2s_buf = nullptr;
};
