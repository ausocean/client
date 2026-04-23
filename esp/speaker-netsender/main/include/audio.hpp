/*
  Name:
    audio.hpp - handling of audio file management.

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

#include "esp_err.h"

#include "tas5805.hpp"

// The length of the hashed filename will always be the same
// due to the constant length hash (64), plus the filetype ('.wav'),
// and the null terminator ('\0').
constexpr const auto HASH_FILENAME_LEN = 69;

/**
 * @brief gets a hashed filename for a given url.
 *
 * @param[in] url to get filename for.
 * @param[out] out_filename hashed filename.
 */
void url_to_filename(const char *url, char *out_filename);

/**
 * @brief downloads the current var.FileName to the SD card.
 */
esp_err_t download_file_to_sdcard();

/**
 * @brief Initialise I2S system, and amplifier.
 *
 * @return a configured TAS5805 amplifier.
 */
TAS5805 init_amp();

/**
 * @brief Audio loop to be run in FreeRTOS task.
 *
 * @param pvParameters pointer to configured amplifier.
 */
void audio_task(void *pvParameters);
