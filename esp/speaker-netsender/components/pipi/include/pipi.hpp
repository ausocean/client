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

#include <cstdarg>
#include <fstream>
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
    friend class FileLogger;

  public:
    /**
     * @brief constructor with timestamp, log level and message.
     *
     * @param[in] ts unix timestamp (seconds)
     * @param[in] level enum log level
     * @param[in] msg format string
     * @param[in] args format arguments
     */
    Entry(const int64_t ts, const Level level, const char *fmt, va_list args);

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

// Path used for logs if none, or bad path provided.
constexpr auto DEFAULT_PATH = "log";

class FileLogger {
  public:
    /**
     * @brief Create a new Pipi FileLogger with the given logging location.
     *
     * @param[in] path directory to store log files.
     */
    FileLogger(const char *path);

    /**
     * @brief Create a new Pipi FileLogger with the default logging location.
     */
    FileLogger();

    /**
     * @brief Log an info level log.
     *
     * @param[in] msg format string for log message.
     * @param[in] args arguments for format string.
     */
    esp_err_t info(const char *msg, va_list args);
    esp_err_t info(const char *msg);

    /**
     * @brief Log a warn level log.
     *
     * @param[in] msg format string for log message.
     * @param[in] args arguments for format string.
     */
    esp_err_t warn(const char *msg, va_list args);
    esp_err_t warn(const char *msg);

    /**
     * @brief Log an error level log.
     *
     * @param[in] msg format string for log message.
     * @param[in] args arguments for format string.
     */
    esp_err_t error(const char *msg, va_list args);
    esp_err_t error(const char *msg);

    /**
     * @brief Log a fatal level log.
     *
     * @param[in] msg format string for log message.
     * @param[in] args arguments for format string.
     */
    esp_err_t fatal(const char *msg, va_list args);
    esp_err_t fatal(const char *msg);

    /**
     * @brief maximum path length.
     */
    static constexpr auto MAX_PATH_LENGTH = 256;

  private:
    friend class TestFileLogger;

    FRIEND_TEST(TestFileLogger, Create_Good_Path);
    FRIEND_TEST(TestFileLogger, Create_With_No_Path);
    FRIEND_TEST(TestFileLogger, Create_Empty_Path);
    FRIEND_TEST(TestFileLogger, Make_Path);
    FRIEND_TEST(TestFileLogger, Write_Logs_Direct);

    /**
     * @brief Log a formatted message for a given log level.
     *
     * @param[in] level enum log level.
     * @param[in] msg format string for log message.
     * @param[in] args arguments for format string.
     */
    esp_err_t log(const Level level, const char *msg, ...);

    /**
     * @brief Create a new log file with a timestamped name.
     *
     * This updates the curr and prev files.
     */
    esp_err_t new_file();

    /**
     * @brief current logging file.
     */
    std::ofstream curr_file;

    /**
     * @brief path to current log file.
     */
    char curr_file_path[MAX_PATH_LENGTH];

    /**
     * @brief previous logging file.
     */
    std::ifstream prev_file;

    /**
     * @brief path for log files.
     */
    char path[MAX_PATH_LENGTH];

    /**
     * @brief make all parent directories of the given path.
     *
     * @param[in] path path to create.
     */
    esp_err_t make_path(const char *path);

    /**
     * @brief tracks if the creation has completed successfully.
     */
    bool ready;

    void close();
};

} // namespace Pipi
