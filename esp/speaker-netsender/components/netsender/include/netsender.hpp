/*
  Name:
    netsender.hpp - An ESP-IDF component to implement the netsender protocol.

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

#pragma once

#include "freertos/projdefs.h"
#include "sdkconfig.h"
#include <esp_netif.h>
#include <esp_types.h>
#include "esp_http_client.h"
#include <optional>
#include <string>

constexpr const auto NETSENDER_MAC_SIZE       = 18;
constexpr const auto NETSENDER_WIFI_SIZE      = 80;
constexpr const auto NETSENDER_PIN_SIZE       = 4;
constexpr const auto NETSENDER_IO_SIZE        = (CONFIG_NETSENDER_MAX_PINS * NETSENDER_PIN_SIZE);
constexpr const auto NETSENDER_MAX_HANDLERS   = 2;
constexpr const auto NETSENDER_VERSION        = "0.1.0";

// Device modes.
namespace netsender_mode {
constexpr auto ONLINE = "Normal";
constexpr auto OFFLINE = "Offline";
};

// Device requests.
enum netsender_request_type_t {
    NETSENDER_REQUEST_TYPE_CONFIG = 0,
    NETSENDER_REQUEST_TYPE_POLL   = 1,
    NETSENDER_REQUEST_TYPE_ACT    = 2,
    NETSENDER_REQUEST_TYPE_VARS   = 3,
};

// Netsender request endpoints.
namespace netsender_endpoint {
static constexpr const auto CONFIG = "/config";
static constexpr const auto POLL = "/poll";
};

// Service response codes.
enum netsender_rc_t {
    NETSENDER_RC_OK      = 0,
    NETSENDER_RC_UPDATE  = 1,
    NETSENDER_RC_REBOOT  = 2,
    NETSENDER_RC_DEBUG   = 3,
    NETSENDER_RC_UPGRADE = 4,
    NETSENDER_RC_ALARM   = 5,
    NETSENDER_RC_TEST    = 6
};

// Boot codes.
enum netsender_boot_code_t {
    NETSENDER_BOOT_CODE_NORMAL = 0x00, // Normal reboot (operator requested).
    NETSENDER_BOOT_CODE_WIFI   = 0x01, // Reboot due to error when trying to disconnect from Wifi.
    NETSENDER_BOOT_CODE_ALARM  = 0x02, // Alarm auto-restart.
};

/**
 * @brief netsender client configuration
 *
 * Configuration parameters are saved to the first 384 bytes of EEPROM
 * for the netsender device respectively as follows:
 *   Version        (length 2)
 *   Mon. period    (length 2)
 *   Act. period    (length 2)
 *   Boot           (length 2)
 *   WiFi ssid,key  (length 80)
 *   Device key     (length 32)
 *   Inputs         (length 80)  * 10 or 20 x 4
 *   Outputs        (length 80)  * 10 or 20 x 4
 *   Reserved       (length to pad to 384 bytes)
 */
struct netsender_configuration_t {
    short version;
    short monPeriod;
    short actPeriod;
    short boot;
    char wifi[NETSENDER_WIFI_SIZE];
    char dkey[CONFIG_NETSENDER_DKEY_SIZE];
    char inputs[NETSENDER_IO_SIZE];
    char outputs[NETSENDER_IO_SIZE];
    char reserved[CONFIG_NETSENDER_RESERVED_SIZE];
};

// Pin represents a pin name and value and optional POST data.
struct netsender_pin_t {
    char name [NETSENDER_PIN_SIZE];
    std::optional<int> value; // std::nullopt indicates no invalid or no value.
    uint8_t * data;
};

// ReaderFunc represents a pin reading function.
typedef std::optional<int> (*ReaderFunc)(netsender_pin_t *);

class Netsender {
public:
    /**
     * @brief initialise the netsender client.
     *
     * @param interface_handle handle to make requests on the interface.
     */
    Netsender();

    /**
     * @brief prints device config.
     */
    constexpr void print_config() const;

    /**
     * @brief makes a request to get variables.
     *
     * This function should be called
     */
    esp_err_t heartbeat();

    /**
     * @brief start netsender task.
     */
    void start();

    ~Netsender();

private:

    /**
     * Netsender has been configured.
     */
    bool configured;

    /**
     * @brief internal buffer for HTTP response.
     */
    char resp_buf[CONFIG_NETSENDER_MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    /**
     * @brief maximum allowed length for a request url.
     */
    static constexpr const int max_url_len = 148;

    /**
     * @brief url used to make requests.
     */
    char url[max_url_len + 1];

    /**
     * Netsender Configuration.
     */
    netsender_configuration_t config{};

    /**
     * string formatted MAC.
     */
    char mac[18];

    /**
     * @brief wraps run function to create new task.
     */
    static void task_wrapper(void *params);

    /**
     * @brief main run loop of the netsender client.
     *
     * run is called by task_wrapper, to run the loop
     * in a task.
     */
    void run();

    /**
     * @brief makes a config request to the remote server.
     *
     * @sideeffect updates the instance config and NVS storage
     * if the returned config has changed.
     */
    esp_err_t req_config();

    /**
     * @brief makes a poll request to the remote server.
     */
    esp_err_t req_poll();

    /**
     * @brief makes a vars request to the remote server.
     */
    esp_err_t req_vars();

    /**
     * @brief handles response codes from server request.
     */
    esp_err_t handle_response_code(std::string rc);

    /**
     * Read the config from non-volatile storage (NVS).
     */
    esp_err_t read_nvs_config();

    /**
     * Write the config to non-volatile storage (NVS).
     */
    esp_err_t write_nvs_config();

    /**
     * @brief returns time in seconds since last reboot.
     */
    int64_t uptime() const;

    /**
     * Pins for inputs and outputs.
     */
    netsender_pin_t inputs[CONFIG_NETSENDER_MAX_PINS];
    netsender_pin_t outputs[CONFIG_NETSENDER_MAX_PINS];
};
