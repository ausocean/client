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
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ostream>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"

// Logging Tag.
static constexpr auto TAG = "Pipi";

Pipi::Entry::Entry(const int64_t ts, const Level level, const char *msg) : timestamp(ts), level(level), data(msg) {}

esp_err_t Pipi::Entry::write(std::ostream &stream)
{
    int len = strlen(data);
    if (len > this->MAX_LOG_LENGTH) {
        len = this->MAX_LOG_LENGTH;
    } else if (len < 0) {
        return ESP_FAIL;
    }
    char marshalled[Pipi::Entry::MAX_LOG_LENGTH + 100];
    auto written = snprintf(marshalled, Pipi::Entry::MAX_LOG_LENGTH + 100,
                            "{"
                            "\"timestamp\":%" PRId64 ","
                            "\"level\":%d,"
                            "\"message\":\"%s\""
                            "}",
                            this->timestamp, this->level, this->data);
    if (written < 0 || written >= sizeof(marshalled)) {
        return ESP_FAIL;
    }

    stream.write(marshalled, written);
    if (stream.fail()) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t Pipi::FileLogger::make_path(const char *path)
{
    auto path_len = strnlen(path, MAX_PATH_LENGTH + 1);
    if (path_len > MAX_PATH_LENGTH - 1) {
        ESP_LOGE(TAG, "path too long");
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
            ESP_LOGE(TAG, "unable to create dir (%s): %d (%s)", parent, errno, strerror(errno));
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

Pipi::FileLogger::FileLogger(const char *path) : ready(false)
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
            ESP_LOGE(TAG, "unable to create path");
            return;
        }
        strncpy(this->path, resolved, this->MAX_PATH_LENGTH);
    }

    // Start a new logging file.
    auto err = this->new_file();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to start new logfile: %s", esp_err_to_name(err));
        return;
    }

    ready = true;
}

Pipi::FileLogger::FileLogger() : Pipi::FileLogger(DEFAULT_PATH) {}

esp_err_t Pipi::FileLogger::new_file()
{
    if (this->prev_file.is_open()) {
        this->prev_file.close();
    }
    if (this->curr_file.is_open()) {
        this->curr_file.close();
        this->prev_file.open(curr_file_path);
        if (!this->prev_file.is_open()) {
            ESP_LOGE(TAG, "unable to open previous file as ifstream");
            return ESP_FAIL;
        }
    }

    snprintf(this->curr_file_path, MAX_PATH_LENGTH, "%s/%" PRId64 ".log", this->path, time(nullptr));
    this->curr_file.open(this->curr_file_path, std::ios::out);

    if (!this->curr_file.is_open()) {
        ESP_LOGE(TAG, "unable to open log file: %s", this->curr_file_path);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Pipi::FileLogger::info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return this->log(Level::INFO, fmt, args);
}
esp_err_t Pipi::FileLogger::warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return this->log(Level::WARN, fmt, args);
}
esp_err_t Pipi::FileLogger::error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return this->log(Level::ERROR, fmt, args);
}
esp_err_t Pipi::FileLogger::fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return this->log(Level::FATAL, fmt, args);
}

esp_err_t Pipi::FileLogger::log(const Pipi::Level level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (!this->ready) {
        va_end(args);
        return ESP_FAIL;
    }

    char msg[Entry::MAX_LOG_LENGTH];
    auto written = vsnprintf(msg, Entry::MAX_LOG_LENGTH, fmt, args);
    va_end(args);
    if (written < 0) {
        ESP_LOGE(TAG, "unable to format log message");
        return ESP_FAIL;
    }

    std::chrono::system_clock::now();
    auto e = Entry(time(nullptr), level, msg);

    return e.write(curr_file);
}

void Pipi::FileLogger::close()
{
    if (this->curr_file.is_open()) {
        this->curr_file.close();
    }
    if (this->prev_file.is_open()) {
        this->prev_file.close();
    }
    this->ready = false;
}
