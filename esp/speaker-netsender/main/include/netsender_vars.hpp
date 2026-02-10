#pragma once
#include <array>
#include <string>
#include <cstring>

// Generated for esp-speaker v0.0.1

namespace netsender {

enum var_type_t {
    BYTE = 0,
    STRING = 1,
};

constexpr const auto VAR_COUNT = 2;

namespace netsender_var {
constexpr const auto VAR_ID_VOLUME = "Volume";
constexpr const auto VAR_ID_FILEPATH = "FilePath";
}

constexpr const auto VARIABLES = std::array{
    netsender_var::VAR_ID_VOLUME,
    netsender_var::VAR_ID_FILEPATH,
};

struct device_var_state_t {
    char Volume;
    char FilePath[64];
};

inline void update_state_member(device_var_state_t &state, int var_index, const std::string& val)
{
    switch (var_index) {
    case 0:
        state.Volume = static_cast<char>(std::stoi(val));
        break;
    case 1:
        strncpy(state.FilePath, val.c_str(), sizeof(state.FilePath) - 1);
        break;
    };
};
} // namespace netsender