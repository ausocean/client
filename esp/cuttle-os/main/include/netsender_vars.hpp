#pragma once
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

#include "esp_err.h"
#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"

// Generated for cuttle-os v0.0.1

namespace netsender {

const constexpr auto ICD_VERSION = "v0.0.1";

const constexpr auto MAX_STR_VAR_LEN = 512;

enum var_type_t {
    BYTE = 0,
    STRING = 1,
    INT32 = 2,
};

constexpr const auto VAR_COUNT = 13;

namespace var {
constexpr const auto VAR_ID_VS = "vs";
constexpr const auto VAR_ID_LOGLEVEL = "LogLevel";
constexpr const auto VAR_ID_PULSES = "Pulses";
constexpr const auto VAR_ID_PULSEWIDTH = "PulseWidth";
constexpr const auto VAR_ID_PULSEDUTYCYCLE = "PulseDutyCycle";
constexpr const auto VAR_ID_PULSECYCLE = "PulseCycle";
constexpr const auto VAR_ID_AUTORESTART = "AutoRestart";
constexpr const auto VAR_ID_ALARMPERIOD = "AlarmPeriod";
constexpr const auto VAR_ID_ALARMNETWORK = "AlarmNetwork";
constexpr const auto VAR_ID_ALARMVOLTAGE = "AlarmVoltage";
constexpr const auto VAR_ID_ALARMRECOVERYVOLTAGE = "AlarmRecoveryVoltage";
constexpr const auto VAR_ID_PEAKVOLTAGE = "PeakVoltage";
constexpr const auto VAR_ID_HEARTBEATPERIOD = "HeartbeatPeriod";
} // namespace var

constexpr const auto VARIABLES = std::array{
    var::VAR_ID_VS,
    var::VAR_ID_LOGLEVEL,
    var::VAR_ID_PULSES,
    var::VAR_ID_PULSEWIDTH,
    var::VAR_ID_PULSEDUTYCYCLE,
    var::VAR_ID_PULSECYCLE,
    var::VAR_ID_AUTORESTART,
    var::VAR_ID_ALARMPERIOD,
    var::VAR_ID_ALARMNETWORK,
    var::VAR_ID_ALARMVOLTAGE,
    var::VAR_ID_ALARMRECOVERYVOLTAGE,
    var::VAR_ID_PEAKVOLTAGE,
    var::VAR_ID_HEARTBEATPERIOD,
};

struct device_var_state_t {
    int32_t vs;
    int32_t LogLevel;
    int32_t Pulses;
    int32_t PulseWidth;
    int32_t PulseDutyCycle;
    int32_t PulseCycle;
    int32_t AutoRestart;
    int32_t AlarmPeriod;
    int32_t AlarmNetwork;
    int32_t AlarmVoltage;
    int32_t AlarmRecoveryVoltage;
    int32_t PeakVoltage;
    int32_t HeartbeatPeriod;
};

static constexpr auto NVS_VAR_KEY = "variables";

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
    } else if (var_id == var::VAR_ID_LOGLEVEL) {
        if (parse_int_var(val, number)) {
            state.LogLevel = number;
        }
    } else if (var_id == var::VAR_ID_PULSES) {
        if (parse_int_var(val, number)) {
            state.Pulses = number;
        }
    } else if (var_id == var::VAR_ID_PULSEWIDTH) {
        if (parse_int_var(val, number)) {
            state.PulseWidth = number;
        }
    } else if (var_id == var::VAR_ID_PULSEDUTYCYCLE) {
        if (parse_int_var(val, number)) {
            state.PulseDutyCycle = number;
        }
    } else if (var_id == var::VAR_ID_PULSECYCLE) {
        if (parse_int_var(val, number)) {
            state.PulseCycle = number;
        }
    } else if (var_id == var::VAR_ID_AUTORESTART) {
        if (parse_int_var(val, number)) {
            state.AutoRestart = number;
        }
    } else if (var_id == var::VAR_ID_ALARMPERIOD) {
        if (parse_int_var(val, number)) {
            state.AlarmPeriod = number;
        }
    } else if (var_id == var::VAR_ID_ALARMNETWORK) {
        if (parse_int_var(val, number)) {
            state.AlarmNetwork = number;
        }
    } else if (var_id == var::VAR_ID_ALARMVOLTAGE) {
        if (parse_int_var(val, number)) {
            state.AlarmVoltage = number;
        }
    } else if (var_id == var::VAR_ID_ALARMRECOVERYVOLTAGE) {
        if (parse_int_var(val, number)) {
            state.AlarmRecoveryVoltage = number;
        }
    } else if (var_id == var::VAR_ID_PEAKVOLTAGE) {
        if (parse_int_var(val, number)) {
            state.PeakVoltage = number;
        }
    } else if (var_id == var::VAR_ID_HEARTBEATPERIOD) {
        if (parse_int_var(val, number)) {
            state.HeartbeatPeriod = number;
        }
    }
}

inline esp_err_t write_vars_to_file(const device_var_state_t &state, const char *file_path)
{
    FILE *fd = fopen(file_path, "w");
    if (fd == NULL) return ESP_FAIL;

    if (fprintf(fd, "%s:%d\n", var::VAR_ID_VS, state.vs) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_LOGLEVEL, state.LogLevel) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_PULSES, state.Pulses) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_PULSEWIDTH, state.PulseWidth) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_PULSEDUTYCYCLE, state.PulseDutyCycle) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_PULSECYCLE, state.PulseCycle) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_AUTORESTART, state.AutoRestart) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_ALARMPERIOD, state.AlarmPeriod) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_ALARMNETWORK, state.AlarmNetwork) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_ALARMVOLTAGE, state.AlarmVoltage) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_ALARMRECOVERYVOLTAGE, state.AlarmRecoveryVoltage) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_PEAKVOLTAGE, state.PeakVoltage) < 0) {
        fclose(fd);
        return ESP_FAIL;
    }
    if (fprintf(fd, "%s:%d\n", var::VAR_ID_HEARTBEATPERIOD, state.HeartbeatPeriod) < 0) {
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

inline esp_err_t write_vars_to_nvs(const device_var_state_t &state)
{
    auto err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t nvs_handle;

    err = nvs_open("netsender", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    auto var_size = sizeof(device_var_state_t);
    err = nvs_set_blob(nvs_handle, NVS_VAR_KEY, &state, var_size);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_close(nvs_handle);

    return ESP_OK;
}

inline std::optional<device_var_state_t> read_vars_from_nvs()
{
    auto err = nvs_flash_init();
    if (err != ESP_OK) {
        return std::nullopt;
    }

    // New handle to access non-volatile storage.
    nvs_handle_t nvs_handle;

    // Open non-volatile storage and attach to handle.
    err = nvs_open("netsender", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return std::nullopt;
    }

    device_var_state_t vars;
    size_t var_size;
    err = nvs_get_blob(nvs_handle, NVS_VAR_KEY, &vars, &var_size);
    if (var_size != sizeof(device_var_state_t)) {
        return std::nullopt;
    }

    return vars;
}
} // namespace netsender
