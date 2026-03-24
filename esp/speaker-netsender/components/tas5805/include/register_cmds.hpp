/*
  Name:
    register_cmds.hpp - values for each registers commands.

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

#include <stdint.h>
#include "tas5805.hpp"

enum class PAGE : uint8_t {
    ZERO = 0x00,
};

enum class BOOK : uint8_t {
    ZERO = 0x00,
};

enum class CTRL_STATE : uint8_t {
    DEEP_SLEEP = 0b00,
    SLEEP      = 0b01,
    HI_Z       = 0b10,
    PLAY       = 0b11,
};

enum class DAMP_PBTL : uint8_t {
    BTL_MODE  = 0b0 << 2,
    PBTL_MODE = 0b1 << 2,
};

enum class ANA_GAIN : uint8_t {
    DB_0 = 0b00000,
};
