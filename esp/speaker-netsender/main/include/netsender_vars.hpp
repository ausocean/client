#pragma once
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"

// Generated for esp-speaker v1

namespace netsender {

const constexpr auto ICD_VERSION = "v1";

const constexpr auto MAX_STR_VAR_LEN = 512;

enum var_type_t {
    BYTE = 0,
    STRING = 1,
};

constexpr const auto VAR_COUNT = 2;

namespace var {
constexpr const auto VAR_ID_VOLUME = "Volume";
constexpr const auto VAR_ID_FILEPATH = "FilePath";
}

constexpr const auto VARIABLES = std::array{
    var::VAR_ID_VOLUME,
    var::VAR_ID_FILEPATH,
};

struct device_var_state_t {
    char Volume;
    char FilePath[MAX_STR_VAR_LEN];
};

inline void update_state_member(device_var_state_t &state, const std::string& var_id, const std::string& val)
{
    if (var_id == var::VAR_ID_VOLUME) {
        state.Volume = static_cast<char>(std::stoi(val));
    } else if (var_id == var::VAR_ID_FILEPATH) {
        strncpy(state.FilePath, val.c_str(), sizeof(state.FilePath) - 1);
        state.FilePath[sizeof(state.FilePath) - 1] = '\0';
    }
}

inline esp_err_t write_vars_to_file(const device_var_state_t &state, const std::string& file_path)
{
    FILE* fd = fopen(file_path.c_str(), "w");
    if (fd == NULL) {
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

inline std::optional<device_var_state_t> read_vars_from_file(const std::string& file_path)
{
    device_var_state_t vars = {};

    ESP_LOGI("NETSENDER_VARS", "opening file");
    FILE* fd = fopen(file_path.c_str(), "r");
    if (fd == NULL) {
        return std::nullopt;
    }
    ESP_LOGI("NETSENDER_VARS", "file opened");

    // This is longer than we need, however, we don't have a standard
    // YET for how long a variable name can be.
    static char out[2 * MAX_STR_VAR_LEN + 1];
    char* value = NULL;
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
        value++; // Point to the start of the second string (value).

        ESP_LOGI("NETSENDER_VARS", "got variable: %s = %s", out, value);
        update_state_member(vars, out, value);
    }

    fclose(fd);
    return vars;
}
} // namespace netsender
