/*
  Name:
    pipi.cpp - An ESP-IDF component to implement a logging system.

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

#include "include/pipi.hpp"

#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"

// Logging Tag.
static constexpr auto TAG = "Pipi";

Pipi::Entry::Entry(const int64_t ts, const Level level, const char *msg) : timestamp(ts), level(level), data(msg) {}

esp_err_t Pipi::Entry::write(int fd)
{
    int len = strlen(data);
    if (len > this->MAX_LOG_LENGTH) {
        len = this->MAX_LOG_LENGTH;
    } else if (len < 0) {
        return ESP_FAIL;
    }

    // Keep in sync with level enums.
    auto LevelStrings = {"info", "warn", "error", "fatal"};

    char marshalled[Pipi::Entry::MAX_LOG_LENGTH + 100];
    auto marshalled_len = snprintf(marshalled, Pipi::Entry::MAX_LOG_LENGTH + 100,
                                   "{"
                                   "\"caller\":\"speaker-netsender\","
                                   "\"timestamp\":%" PRId64 ","
                                   "\"level\":\"%s\","
                                   "\"message\":\"%s\""
                                   "}\n",
                                   this->timestamp, LevelStrings.begin()[this->level], this->data);
    if (marshalled_len < 0 || marshalled_len >= sizeof(marshalled)) {
        return ESP_FAIL;
    }

    auto written = ::write(fd, marshalled, marshalled_len);
    if (written != marshalled_len) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t Pipi::FileLogger::make_path(const char *path)
{
    auto path_len = strnlen(path, MAX_PATH_LENGTH + 1);
    if (path_len > MAX_PATH_LENGTH - 1) {
        printf("%s: path too long", TAG);
        return ESP_FAIL;
    }

    auto making = true;
    char parent[Pipi::FileLogger::MAX_PATH_LENGTH];
    errno = 0;
    auto loc = path;
    while (making) {
        // find next directory string.
        loc = strchr(&loc[1], '/');
        if (loc == NULL) {
            loc = &path[strlen(path)];
            making = false;
        }

        auto len = loc - path;
        snprintf(parent, len + 1, "%s", path);

        auto status = mkdir(parent, 0777);
        if (status == -1 && errno != EEXIST) {
            printf("%s: unable to create dir (%s): %d (%s)", TAG, parent, errno, strerror(errno));
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

Pipi::FileLogger::FileLogger(const char *path) : curr_file(-1), prev_file(-1), ready(false)
{
    // Validate the path is a directory.
    const char *resolved = (path != nullptr && path[0] != '\0') ? path : DEFAULT_PATH;
    struct stat buffer{};
    errno = 0;
    auto status = stat(resolved, &buffer);

    if (status == 0 && S_ISDIR(buffer.st_mode)) {
        snprintf(this->path, sizeof(this->path), "%s", resolved);
    } else if (status == -1 && errno == ENOENT) {
        // Directory doesn't exist so create it.
        auto err = make_path(resolved);
        if (err != ESP_OK) {
            printf("%s: unable to create path", TAG);
            return;
        }
        strncpy(this->path, resolved, this->MAX_PATH_LENGTH);
    }

    // Start a new logging file.
    auto err = this->new_file();
    if (err != ESP_OK) {
        printf("%s: unable to start new logfile: %s", TAG, esp_err_to_name(err));
        return;
    }

    ready = true;
}

Pipi::FileLogger::FileLogger() : Pipi::FileLogger(DEFAULT_PATH) {}

esp_err_t Pipi::FileLogger::new_file()
{
    if (this->prev_file >= 0) {
        if (::close(this->prev_file) != 0) {
            printf("%s: unable to close previous file: %s (%d)", TAG, strerror(errno), errno);
            return ESP_FAIL;
        }
        this->prev_file = -1;
    }
    if (this->curr_file >= 0) {
        this->prev_file = this->curr_file;
        this->curr_file = -1;
    }

    static int log_counter = 0;
    snprintf(this->curr_file_path, MAX_PATH_LENGTH, "%s/test_%d.log", this->path, log_counter++);
    this->curr_file = ::open(this->curr_file_path, O_CREAT | O_RDWR | O_TRUNC);
    if (this->curr_file < 0) {
        printf("%s: unable to open new file (%s): %s (%d)", TAG, this->curr_file_path, strerror(errno), errno);
        this->curr_file = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Pipi::FileLogger::log(char *msg)
{
    // Example log:
    // I (01:02:03.040) <tag>: <log message>
    //                  ^
    //                  |
    //            17th character
    constexpr auto msg_start = 17;

    // Determine log level based on first character.
    auto level = INFO;
    switch (msg[0]) {
    case 'I':
        level = INFO;
        break;
    case 'W':
        level = WARN;
        break;
    case 'E':
        level = ERROR;
        break;
    case 'F':
        level = FATAL;
        break;
    }

    // Cut the level and time from the passed message.
    auto cut_log = &msg[msg_start];
    cut_log[strlen(cut_log) - 1] = '\0';

    std::chrono::system_clock::now();
    auto e = Entry(time(nullptr), level, cut_log);

    return e.write(curr_file);
}

int Pipi::FileLogger::get_logs()
{
    ESP_ERROR_CHECK(this->new_file());
    return this->prev_file;
}

void Pipi::FileLogger::close()
{
    if (this->prev_file >= 0) {
        if (::close(this->prev_file) != 0) {
            printf("%s: unable to close previous file: %s (%d)", TAG, strerror(errno), errno);
        }
        this->prev_file = -1;
    }
    if (this->curr_file >= 0) {
        if (::close(this->curr_file) != 0) {
            printf("%s: unable to close current file: %s (%d)", TAG, strerror(errno), errno);
        }
        this->curr_file = -1;
    }
    this->ready = false;
}
