/*
  Name:
    globals.h - global variables.

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

#include <atomic>
#include <mutex>

#include "include/netsender_vars.hpp"

// Mount point for the SD card filesystem.
inline const constexpr auto MOUNT_POINT = "/sdcard";

// Request the audio task to stop the current file and restart with the URL in
// current_audio_url. The main task sets this after a successful download and
// only the audio task clears it. This avoids a switch being lost if the audio
// task is blocked on SD I/O for a while.
inline std::atomic<bool> reload_requested{false};

extern netsender::device_var_state_t vars;
extern std::mutex vars_mutex;
extern char current_audio_url[netsender::MAX_STR_VAR_LEN];

// Set when the variables have changed and need to be persisted to SD. The
// audio task performs the write between tracks so it never competes with
// playback, and the HTTP poll path never touches the SD card.
inline std::atomic<bool> vars_dirty{false};

/**
 * @brief Persist the variables to SD if they have changed.
 *
 * Must only be called when audio is not being fed to I2S.
 */
esp_err_t persist_vars_if_dirty();
