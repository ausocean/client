#include <cstdint>
#include <optional>

#include "esp_err.h"

#pragma once

/**
 * @brief Victron solar charger operating states (VE_REG_DEVICE_STATE).
 */
enum victron_device_state_t : uint8_t {
    VICTRON_STATE_OFF = 0,
    VICTRON_STATE_FAULT = 2,
    VICTRON_STATE_BULK = 3,
    VICTRON_STATE_ABSORPTION = 4,
    VICTRON_STATE_FLOAT = 5,
    VICTRON_STATE_EQUALISE = 7,
    VICTRON_STATE_STARTING_UP = 245,
    VICTRON_STATE_RECONDITION = 247,
    VICTRON_STATE_EXTERNAL_CONTROL = 252,
    VICTRON_STATE_NA = 255,
};

/**
 * @brief Telemetry data from a Victron Solar Charger.
 *
 * Stores current device state, power usage, and solar generation data.
 */
struct victron_solar_data_t {
    uint8_t device_state;      /**< Current operating state (@ref victron_device_state_t) */
    uint8_t charger_error;     /**< Active charger errors */
    int16_t battery_voltage;   /**< Battery Terminal voltage (0.01V increments) */
    int16_t battery_current;   /**< Battery Current (0.1A increments) */
    int16_t load_current;      /**< Load Current (0.1A increments) */
    int16_t solar_yield_today; /**< Total solar yield generated today (0.01kWh increments) */
    int16_t solar_power;       /**< Current solar generation power (1W increments) */
};

esp_err_t victron_ble_init();

std::optional<victron_solar_data_t> victron_ble_get_data();
