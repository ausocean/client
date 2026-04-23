/*
  Name:
    pipi.hpp - An ESP-IDF component to implement a logging system.

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

#include <gtest/gtest_prod.h>
#include <ostream>

#include "esp_err.h"

namespace Pipi {

/**
 * @brief log severity levels
 */
enum Level {
    INFO,
    WARN,
    ERROR,
    FATAL,
};

class TestEntry;

/**
 * @brief log entry for Pipi Logger.
 *
 * Entry is a container for a log entry, containing a unix timestamp, log
 * severity level and the text data.
 */
class Entry {
  public:
    /**
     * @brief constructor with timestamp, log level and message.
     *
     * @param[in] ts unix timestamp (seconds)
     * @param[in] level enum log level
     * @param[in] msg log message
     */
    Entry(const int64_t ts, const Level level, const char *msg);

    /**
     * @brief write JSON formatted Entry to stream.
     *
     * Writing the Entry to the given stream will format the entry as a single
     * JSON entity to write to the stream.
     *
     * @param[in] stream an output stream such as a file to write the formatted
     * log to
     */
    esp_err_t write(std::ostream &stream);

  private:
    /**
     * Used for testing the Entry class.
     */
    friend class TestEntry;

    /**
     * Test functions.
     */
    FRIEND_TEST(TestEntry, Create);
    FRIEND_TEST(TestEntry, Write);

    /**
     * @brief maximum length for a log.
     */
    static constexpr auto MAX_LOG_LENGTH = 256;

    /**
     * @brief unix timestamp.
     */
    int64_t timestamp;

    /**
     * @brief severity level.
     */
    Level level;

    /**
     * @brief Text Data.
     */
    const char *data;
};

} // namespace Pipi
