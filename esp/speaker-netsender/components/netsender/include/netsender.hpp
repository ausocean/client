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

#define NETSENDER_MAC_SIZE     18
#define NETSENDER_WIFI_SIZE    80
#define NETSENDER_PIN_SIZE     4
#define NETSENDER_IO_SIZE      (CONFIG_NETSENDER_MAX_PINS * NETSENDER_PIN_SIZE)
#define NETSENDER_MAX_HANDLERS 2
#define NETSENDER_VERSION      "0.1.0"

// Device modes.
constexpr const char* NETSENDER_MODE_ONLINE = "Normal";
constexpr const char* NETSENDER_MODE_OFFLINE = "Offline";

// Device requests.
typedef enum {
    netsender_request_type_config = 0,
    netsender_request_type_poll   = 1,
    netsender_request_type_act    = 2,
    netsender_request_type_vars   = 3,
} netsender_request_type_t;

// Netsender request endpoints.
static const char* netsender_endpoint_config = "/config";
static const char* netsender_endpoint_poll = "/poll";

// Service response codes.
typedef enum {
    netsender_rc_ok      = 0,
    netsender_rc_update  = 1,
    netsender_rc_reboot  = 2,
    netsender_rc_debug   = 3,
    netsender_rc_upgrade = 4,
    netsender_rc_alarm   = 5,
    netsender_rc_test    = 6
}  netsender_rc_t;

// Boot codes.
typedef enum {
    netsender_boot_code_normal = 0x00, // Normal reboot (operator requested).
    netsender_boot_code_wiFi   = 0x01, // Reboot due to error when trying to disconnect from Wifi.
    netsender_boot_code_alarm  = 0x02, // Alarm auto-restart.
}  netsender_boot_code_t;

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
typedef struct {
    short version;
    short monPeriod;
    short actPeriod;
    short boot;
    char wifi[NETSENDER_WIFI_SIZE];
    char dkey[CONFIG_NETSENDER_DKEY_SIZE];
    char inputs[NETSENDER_IO_SIZE];
    char outputs[NETSENDER_IO_SIZE];
    char reserved[CONFIG_NETSENDER_RESERVED_SIZE];
} netsender_configuration_t;

// Pin represents a pin name and value and optional POST data.
typedef struct {
    char name [NETSENDER_PIN_SIZE];
    std::optional<int> value; // std::nullopt indicates no invalid or no value.
    uint8_t * data;
} netsender_pin_t;

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
    void print_config();

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
    static const int max_url_len = 148;

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
     * @brief makes a act request to the remote server.
     */
    esp_err_t req_act();

    /**
     * @brief makes a vars request to the remote server.
     */
    esp_err_t req_vars();

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
    int64_t uptime();

    /**
     * Pins for inputs and outputs.
     */
    netsender_pin_t inputs[CONFIG_NETSENDER_MAX_PINS];
    netsender_pin_t outputs[CONFIG_NETSENDER_MAX_PINS];
};
