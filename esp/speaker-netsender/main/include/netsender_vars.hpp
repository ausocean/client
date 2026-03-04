#pragma once
#include <array>
#include <string>
#include <cstring>
#include "esp_err.h"

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
} // namespace netsender
