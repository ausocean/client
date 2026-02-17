#pragma once
#include <array>
#include <string>
#include <cstring>

// Generated for esp-speaker v1

namespace netsender {

const constexpr auto ICD_VERSION = "v1";

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
    char FilePath[64];
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
} // namespace netsender
