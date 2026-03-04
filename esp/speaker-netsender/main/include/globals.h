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

#include "include/netsender_vars.hpp"

// Mount point for the SD card filesystem.
inline const constexpr auto MOUNT_POINT = "/sdcard";

// Atomic flag for stopping audio playback.
// TODO: Use a better threadsafe option.
inline volatile bool reload_requested = false;

extern netsender::device_var_state_t vars;
