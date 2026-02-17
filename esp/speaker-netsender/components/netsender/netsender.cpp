/*
  Name:
    netsender.cpp - An ESP-IDF component to implement the netsender protocol.

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

#include "netsender.hpp"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/param.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_tls.h"

static constexpr const auto MAX_HTTP_RECV_BUFFER = 512;
static constexpr const auto MAX_URL_LEN          = 256;
static constexpr const auto STORAGE_NAMESPACE    = "netsender";
static constexpr const auto CONFIG_NVS_KEY       = "config";

static constexpr const char *TAG = "netsender";

// Static definition of the netsender task stack.
static StackType_t ns_stack[CONFIG_NETSENDER_TASK_STACK_DEPTH];
static StaticTask_t xTaskBuffer;

// Forward Declarations.

// pad_copy copies a string, padding with null characters
void pad_copy(char * dst, const char * src, size_t size);

// check_pins returns the number of valid comma-separated pin names, or
// -1 if any pin is invalid or the number of pins exceeds MAX_PINS.
int check_pins(const char * names);

// is_valid_pin_name returns true if the given name is the letter A,B, D,
// T, or X followed by one or two digits, or false otherwise. If len is
// zero, the name must be null-terminated.
bool is_valid_pin_name(const char *name, size_t len);

Netsender::Netsender()
{
    esp_err_t err;

    // Get Ethernet MAC.
    static constexpr auto MAC_LENGTH = 17;
    uint8_t mac[6];
    esp_mac_type_t mac_type = ESP_MAC_ETH;
    ESP_ERROR_CHECK(esp_read_mac(mac, mac_type));
    snprintf(this->mac, MAC_LENGTH + 1, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Initialise Non-Volatile Storage.
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = read_nvs_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to read configuration from EEPROM: %s", esp_err_to_name(err));
        this->configured = false;
    } else {
        this->configured = true;
    }

}

esp_err_t Netsender::read_nvs_config()
{
    // Create a new handle to access non-volatile storage (NVS).
    nvs_handle_t nvs_handle;

    // Open the nvs and attach to the handle.
    auto err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Read the config from the handle.
    auto config_size = sizeof(netsender_configuration_t);
    err = nvs_get_blob(nvs_handle, CONFIG_NVS_KEY, &this->config, &config_size);
    if (err != ESP_OK) {
        return err;
    }

    // Close the nvs.
    nvs_close(nvs_handle);

    return ESP_OK;
}

esp_err_t Netsender::write_nvs_config()
{
    // Create a new handle to access non-volatile storage (NVS).
    nvs_handle_t nvs_handle;

    // Open the nvs and attach to the handle.
    auto err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Write the config to the handle.
    static constexpr const auto config_size = sizeof(netsender_configuration_t);
    err = nvs_set_blob(nvs_handle, CONFIG_NVS_KEY, &this->config, config_size);
    if (err != ESP_OK) {
        return err;
    }

    // Commit the change.
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Close the nvs.
    nvs_close(nvs_handle);

    return ESP_OK;
}

constexpr void Netsender::print_config() const
{
    static constexpr const auto CONFIG_SIZE = sizeof(netsender_configuration_t);
    ESP_LOGI(TAG, "--- CONFIG ---");
    ESP_LOGI(TAG, "Netsender v%s", NETSENDER_VERSION);
    ESP_LOGI(TAG, "MAC Address: %s", this->mac);
    ESP_LOGI(TAG, "Configuration size: %d", CONFIG_SIZE);
    if (this->configured) {
        ESP_LOGI(TAG, "boot: %d", this->config.boot);
        ESP_LOGI(TAG, "wifi: %s", this->config.wifi);
        ESP_LOGI(TAG, "dkey: %s", this->config.dkey);
        ESP_LOGI(TAG, "monPeriod: %d", this->config.monPeriod);
        ESP_LOGI(TAG, "actPeriod: %d", this->config.actPeriod);
        ESP_LOGI(TAG, "inputs: %d", this->config.inputs);
        ESP_LOGI(TAG, "outputs: %d", this->config.outputs);
    } else {
        ESP_LOGI(TAG, "unconfigured device...");
    }
}

esp_err_t Netsender::register_input(char pin_name[NETSENDER_PIN_SIZE], std::function<std::optional<int64_t>()> read_func)
{
    if (input_cnt >= CONFIG_NETSENDER_MAX_PINS) {
        ESP_LOGE(TAG, "cannot register more than %d inputs", CONFIG_NETSENDER_MAX_PINS);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "registering new input: %s", pin_name);

    auto pin = netsender_pin_t{};
    memcpy(pin.name, pin_name, NETSENDER_PIN_SIZE);
    pin.read = read_func;

    inputs[input_cnt] = pin;
    input_cnt++;

    return ESP_OK;
}

esp_err_t Netsender::register_variable_parser(std::function<esp_err_t(std::string)> parser_func)
{
    ESP_LOGD(TAG, "registering new variable callback");

    this->parse_variable_callback = parser_func;

    return ESP_OK;
}

void Netsender::run()
{
    print_config();

    TickType_t last_poll = 0;
    TickType_t last_sleep = 0;
    short seconds_since_poll = 0;
    short seconds_awake = 0;
    while (true) {
        // Sleep client if active time has surpassed actPeriod.
        seconds_awake = (xTaskGetTickCount() - last_sleep) * portTICK_PERIOD_MS / 1000;
        if (seconds_awake >= this->config.actPeriod) {
            // TODO: put into deep sleep until next poll is required.
            seconds_awake = 0;
            last_sleep = xTaskGetTickCount();
        }

        seconds_since_poll = (xTaskGetTickCount() - last_poll) * portTICK_PERIOD_MS / 1000;
        if (seconds_since_poll >= this->config.monPeriod) {
            req_poll();
            last_poll = xTaskGetTickCount();
            seconds_since_poll = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));

    }
}

void Netsender::task_wrapper(void *params)
{
    Netsender* instance = static_cast<Netsender*>(params);
    instance->run();
}

void Netsender::start()
{
    // Structure that will hold the TCB of the task being created
    xTaskCreateStatic(this->task_wrapper, "NetSender", CONFIG_NETSENDER_TASK_STACK_DEPTH, this, 0, ns_stack, &xTaskBuffer);
}

Netsender::~Netsender() {}

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        {
            // Clean the buffer in case of a new request.
            if (output_len == 0 && evt->user_data) {
                memset(evt->user_data, 0, CONFIG_NETSENDER_MAX_HTTP_OUTPUT_BUFFER);
            }

            // NOTE: We will assume that the response is not chunked, as responses from
            // the server should be minimal.

            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data) {
                // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (CONFIG_NETSENDER_MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len) {
                    memcpy((char*)evt->user_data + output_len, evt->data, copy_len);
                }
            } else {
                ESP_LOGE(TAG, "client requests must attach user_data array to handle response body");
            }
            output_len += copy_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL) {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        {
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
        }
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

esp_err_t Netsender::req_config()
{
    char url[MAX_URL_LEN + 1];

    // Create request url.
    snprintf(url, MAX_URL_LEN,
             "%s%s"           // Host and Endpoint
             "?vn=%s"         // Version
             "&ma=%s"         // MAC
             "&dk=0"         // Device Key
             // TODO: Add Local IP
             // "&la=%d.%d.%d.%d" // IP Address
             "&ut=%lld"        // Uptime
             "&md=%s"         // Mode
             "&er=",        // Error
             CONFIG_NETSENDER_REMOTE_HOST,
             netsender_endpoint::CONFIG,
             NETSENDER_VERSION, this->mac,
             uptime(), netsender_mode::ONLINE
            );

    // Initialise the request.
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .event_handler = http_event_handler,
        .user_data = this->resp_buf,
    };
    auto http_handle = esp_http_client_init(&http_config);

    // Send the request.
    auto err = esp_http_client_perform(http_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Check the status code.
    // TODO: Handle other status codes.
    if (auto status_code = esp_http_client_get_status_code(http_handle) != 200) {
        ESP_LOGE(TAG, "got non 200 status code: %d", status_code);
        return ESP_FAIL;
    }

    bool changed = false;

    // Parse the incoming config.
    std::string param;
    std::string json_resp(resp_buf);
    if (netsender_extract_json(json_resp, "mp", param) && std::stoi(param) != this->config.monPeriod) {
        this->config.monPeriod = std::stoi(param);
        ESP_LOGI(TAG, "monPeriod changed: %d", this->config.monPeriod);
        changed = true;
    }
    if (netsender_extract_json(json_resp, "ap", param) && std::stoi(param) != this->config.actPeriod) {
        this->config.actPeriod = std::stoi(param);
        ESP_LOGI(TAG, "actPeriod changed: %d", this->config.actPeriod);
        changed = true;
    }
    if (netsender_extract_json(json_resp, "wi", param) && param != this->config.wifi) {
        pad_copy(this->config.wifi, param.c_str(), NETSENDER_WIFI_SIZE);
        ESP_LOGI(TAG, "wifi changed: %d", this->config.wifi);
        changed = true;
    }
    if (netsender_extract_json(json_resp, "dk", param) && param != this->config.dkey) {
        pad_copy(this->config.dkey, param.c_str(), CONFIG_NETSENDER_DKEY_SIZE);
        ESP_LOGI(TAG, "dkey changed: %d", this->config.dkey);
        changed = true;
    }
    if (netsender_extract_json(json_resp, "ip", param) && param != this->config.inputs) {
        if (check_pins(param.c_str()) >= 0) {
            pad_copy(this->config.inputs, param.c_str(), NETSENDER_IO_SIZE);
            ESP_LOGI(TAG, "inputs changed: %d", this->config.inputs);
            changed = true;
        } else {
            ESP_LOGW(TAG, "invalid inputs: %s", param.c_str());
        }
    }
    if (netsender_extract_json(json_resp, "op", param) && param != this->config.outputs) {
        if (check_pins(param.c_str()) >= 0) {
            pad_copy(this->config.outputs, param.c_str(), NETSENDER_IO_SIZE);
            ESP_LOGI(TAG, "outputs changed: %d", this->config.outputs);
            changed = true;
        } else {
            ESP_LOGW(TAG, "invalid outputs: %s", param.c_str());
        }
    }

    if (changed) {
        this->configured = true;
        write_nvs_config();
        print_config();
    }

    ESP_ERROR_CHECK(esp_http_client_cleanup(http_handle));

    return ESP_OK;
}

void Netsender::append_pin_to_url(char* url, netsender_pin_t &pin)
{
    auto cur_len = strlen(url);

    if (cur_len + 8 >= MAX_URL_LEN) {
        ESP_LOGE(TAG, "appending pin could exceed maximum url length");
        return;
    }

    // Use ? seperator for first param, and & for all others.
    auto sep = strchr(url, '?') == NULL ? "?" : "&";

    snprintf(url + cur_len, (MAX_URL_LEN + 1) - cur_len,
             "%s%s=%lld",
             sep, pin.name, pin.value.value()
            );
}

esp_err_t Netsender::req_poll()
{
    ESP_LOGI(TAG, "--- POLLING ---");

    char url[MAX_URL_LEN + 1];

    // Create request url.
    snprintf(url, sizeof(url),
             "%s%s"    // Domain and Enpoint.
             "?ma=%s"  // MAC Address.
             "&dk=%s"  // Device Key.
             "&ut=%lld", // Uptime.
             CONFIG_NETSENDER_REMOTE_HOST,
             netsender_endpoint::POLL,
             this->mac, this->config.dkey, uptime()
            );

    for (auto i = 0; i < input_cnt; i++) {
        auto &pin = inputs[i];
        pin.value = pin.read();
        if (pin.value.has_value()) {
            ESP_LOGI(TAG, "read pin %s: %d", pin.name, pin.value.value());
            append_pin_to_url(url, pin);
        } else {
            ESP_LOGE(TAG, "failed to read pin %s", pin.name);
        }
    }

    // Initialise the request.
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .event_handler = http_event_handler,
        .user_data = this->resp_buf,
    };
    auto http_handle = esp_http_client_init(&http_config);

    // Send the request.
    auto err = esp_http_client_perform(http_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Check the status code.
    // TODO: Handle other status codes.
    if (auto status_code = esp_http_client_get_status_code(http_handle) != 200) {
        ESP_LOGE(TAG, "got non 200 status code: %d", status_code);
        return ESP_FAIL;
    }

    // TODO: Handle response.
    ESP_LOGI(TAG, "poll response: %s", this->resp_buf);
    std::string rc;
    const auto resp = std::string(resp_buf);
    const auto has_rc = netsender_extract_json(resp, "rc", rc);
    if (has_rc) {
        ESP_LOGD(TAG, "got response code: %s", rc.c_str());
        if (handle_response_code(rc) != ESP_OK) {
            ESP_LOGE(TAG, "failed to handle response code");
        }
    }

    std::string vs;
    const auto has_vs = netsender_extract_json(resp, "vs", vs);
    if (has_vs) {
        ESP_LOGD(TAG, "got varsum: %s", vs.c_str());
        if (std::stoi(vs) != this->varsum) {
            ESP_LOGD(TAG, "varsum changed, getting vars");
            req_vars();
        }
    }

    // Cleanup http_handle.
    ESP_ERROR_CHECK(esp_http_client_cleanup(http_handle));

    return ESP_OK;
}

esp_err_t Netsender::req_vars()
{
    ESP_LOGI(TAG, "--- REQUESTING VARS ---");

    char url[MAX_URL_LEN + 1];

    // Create request url.
    snprintf(url, sizeof(url),
             "%s%s"    // Domain and Enpoint.
             "?ma=%s"  // MAC Address.
             "&dk=%s",  // Device Key.
             CONFIG_NETSENDER_REMOTE_HOST,
             netsender_endpoint::VARS,
             this->mac, this->config.dkey
            );

    // Initialise the request.
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .event_handler = http_event_handler,
        .user_data = this->resp_buf,
    };
    auto http_handle = esp_http_client_init(&http_config);

    // Send the request.
    auto err = esp_http_client_perform(http_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Check the status code.
    // TODO: Handle other status codes.
    if (auto status_code = esp_http_client_get_status_code(http_handle) != 200) {
        ESP_LOGE(TAG, "got non 200 status code: %d", status_code);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "vars response: %s", this->resp_buf);
    std::string vs;
    const auto resp = std::string(resp_buf);

    err = this->parse_variable_callback(resp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to parse variables in callback");
        return ESP_FAIL;
    }

    const auto has_vs = netsender_extract_json(resp, "vs", vs);
    if (has_vs) {
        ESP_LOGD(TAG, "got varsum: %s", vs.c_str());
        // Update varsum with new varsum.
        this->varsum = std::stoi(vs);
    }

    // Cleanup http_handle.
    ESP_ERROR_CHECK(esp_http_client_cleanup(http_handle));

    return ESP_OK;
}

esp_err_t Netsender::handle_response_code(std::string code)
{
    const auto rc = std::stoi(code);

    esp_err_t err;
    switch (rc) {
    case NETSENDER_RC_OK:
        // Do nothing.
        break;
    case NETSENDER_RC_UPDATE:
        err = req_config();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
        }
        break;
    case NETSENDER_RC_REBOOT:
        esp_restart();
        break;
    case NETSENDER_RC_DEBUG:
        // TODO: implement debug?
        break;
    case NETSENDER_RC_UPGRADE:
        // TODO: implement upgrade?
        break;
    case NETSENDER_RC_ALARM:
        // TODO: implement alarm.
        break;
    case NETSENDER_RC_TEST:
        // TODO: implement test.
        break;
    default:
        ESP_LOGE(TAG, "got unexpected response code:");
        return ESP_FAIL;
    }

    return ESP_OK;
}

int64_t Netsender::uptime() const
{
    return esp_timer_get_time() / 1000000;
}

bool netsender_extract_json(const std::string & json, const char* name, std::string & value)
{
    size_t start = json.find(std::string("\"") + name + "\"");
    if (start == std::string::npos) {
        return false;
    }

    start += strlen(name) + 3; // Skip quotes and colon, then skip any whitespace.
    while (start < json.length() && json[start] == ' ') {
        start++;
    }

    if (start >= json.length()) {
        return false;
    }

    size_t finish;
    switch (json[start]) {
    case '-': case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        finish = json.find(',', start);
        if (finish == std::string::npos) {
            finish = json.find('}', start);
        }
        break;
    case '"':
        start++; // Move past the opening quote
        finish = json.find('"', start);
        break;
    default:
        return false;
    }

    if (finish == std::string::npos) {
        finish = json.length();
    }

    value = json.substr(start, finish - start);
    return true;
}

void pad_copy(char * dst, const char * src, size_t size)
{
    int ii = 0;
    for (; ii < size - 1 && ii < strlen(src); ii++) {
        dst[ii] = src[ii];
    }
    for (; ii < size; ii++) {
        dst[ii]  = '\0';
    }
}

int check_pins(const char * names)
{
    const char *start = names;
    int ii = 0;
    while (*start != '\0') {
        const char * finish = strchr(start, ',');
        if (finish == NULL) {
            if (!is_valid_pin_name(start, 0)) {
                return -1;
            }
            ii++;
            break;
        }
        if (!is_valid_pin_name(start, finish - start)) {
            return -1;
        }
        ii++;
        start = finish + 1;
    }
    if (ii > CONFIG_NETSENDER_MAX_PINS) {
        return -1;
    }
    return ii;
}

bool is_valid_pin_name(const char *name, size_t len)
{
    if (len == 0) {
        len = strlen(name);
    }
    if (len > NETSENDER_PIN_SIZE - 1) {
        return false;
    }
    switch (name[0]) {
    case 'A':
    case 'B':
    case 'D':
    case 'T':
    case 'X':
        if (!isdigit(name[1])) {
            return false;
        }
        if (len == 2) {
            return true; // Single-digit pin.
        }
        if (!isdigit(name[2])) {
            return false;
        }
        return true; // Double-digit pin.
    }
    return false;
}
