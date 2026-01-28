/*
  Name:
    tas5805.h - An ESP-IDF component to support the TAS5805 Amplifier module.

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

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include <stdio.h>

constexpr auto TAS8505_CHANGE_PAGE_REG   = 0x00;
constexpr auto TAS8505_CHANGE_BOOK_REG   = 0x7F;
constexpr auto TAS8505_DEVICE_CTRL_1_REG = 0x02;
constexpr auto TAS8505_DEVICE_CTRL_2_REG = 0x03;
constexpr auto TAS8505_DIG_VOL_CTRL_REG  = 0x4C;
constexpr auto TAS8505_AGAIN_REG         = 0x54;

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
    TAS5805(i2c_master_bus_handle_t handle, i2s_chan_handle_t* tx_handle);

    /**
     * @brief Reads PCM data from a file and writes it to the I2S DMA buffer.
     * @param f Pointer to the opened audio file
     */
    void play(const char* path);

    /**
     * @brief destructor.
     */
    ~TAS5805();

private:
    /**
     * @brief Writes the data to the given register.
     *
     * @param reg number of the register to be written.
     * @param data data to be written to the register.
     * @param len length of the data to write.
     */
    void write_reg(const int reg, const uint8_t* data);

    /** I2C master handles */
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;

    /** I2S handles */
    i2s_chan_handle_t* tx_handle;
};
