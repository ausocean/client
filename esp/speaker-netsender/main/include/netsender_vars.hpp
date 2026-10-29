#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

#include "esp_err.h"
#include "esp_log.h"

// Generated for esp-speaker v1

namespace netsender {

const constexpr auto ICD_VERSION = "v1";

const constexpr auto MAX_STR_VAR_LEN = 512;

enum var_type_t {
    BYTE = 0,
    STRING = 1,
    INT32 = 2,
};

constexpr const auto VAR_COUNT = 3;

namespace var {
constexpr const auto VAR_ID_VS = "vs";
constexpr const auto VAR_ID_VOLUME = "Volume";
constexpr const auto VAR_ID_FILEPATH = "FilePath";
} // namespace var

constexpr const auto VARIABLES = std::array{
    var::VAR_ID_VS,
    var::VAR_ID_VOLUME,
    var::VAR_ID_FILEPATH,
};

struct device_var_state_t {
    int32_t vs;
    char Volume;
    char FilePath[MAX_STR_VAR_LEN];
};

inline bool parse_int_var(std::string_view val, int32_t &out)
{
    const auto *begin = val.data();
    const auto *end = begin + val.size();
    auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

inline void update_state_member(device_var_state_t &state, std::string_view var_id, std::string_view val)
{
    int32_t number = 0;
    if (var_id == var::VAR_ID_VS) {
        if (parse_int_var(val, number)) {
            state.vs = number;
        }
    } else if (var_id == var::VAR_ID_VOLUME) {
        if (parse_int_var(val, number)) {
            state.Volume = static_cast<char>(number);
        }
    } else if (var_id == var::VAR_ID_FILEPATH) {
        const auto len = std::min(val.size(), sizeof(state.FilePath) - 1);
        memcpy(state.FilePath, val.data(), len);
        state.FilePath[len] = '\0';
    }
}

inline esp_err_t write_vars_to_file(const device_var_state_t &state, const char *file_path)
{
    FILE *fd = fopen(file_path, "w");
    if (fd == NULL) {
        return ESP_FAIL;
    }

    if (fprintf(fd, "%s:%d\n", var::VAR_ID_VS, state.vs) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_VOLUME, (int)state.Volume) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%s\n", var::VAR_ID_FILEPATH, state.FilePath) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }

    fclose(fd);
    return ESP_OK;
}

inline std::optional<device_var_state_t> read_vars_from_file(const char *file_path)
{
    device_var_state_t vars = {};

    ESP_LOGI("NETSENDER_VARS", "opening file");
    FILE *fd = fopen(file_path, "r");
    if (fd == NULL) {
        return std::nullopt;
    }
    ESP_LOGI("NETSENDER_VARS", "file opened");

    // This is longer than we need, however, we don't have a standard
    // YET for how long a variable name can be.
    static char out[2 * MAX_STR_VAR_LEN + 1];
    char *value = NULL;
    while (fgets(out, 2 * MAX_STR_VAR_LEN, fd) != nullptr) {
        if (strlen(out) >= 2 * MAX_STR_VAR_LEN) {
            printf("err line too long for buffer: '%s' (len: %d)\n", out, strlen(out));
            break;
        }

        // Remove any newline characters.
        out[strcspn(out, "\r\n")] = '\0';

        // Find the seperator.
        value = strchr(out, ':');
        if (value == NULL) {
            ESP_LOGE("NETSENDER_VARS", "unable to find sep (:)\n");
            break;
        }
        *value = '\0'; // Terminate the first string (name).
        value++;       // Point to the start of the second string (value).

        ESP_LOGI("NETSENDER_VARS", "got variable: %s = %s", out, value);
        update_state_member(vars, out, value);
    }

    fclose(fd);
    return vars;
}
} // namespace netsender
