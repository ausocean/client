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
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_tls.h"

#define MAX_HTTP_RECV_BUFFER   512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define STORAGE_NAMESPACE      "netsender"
#define CONFIG_NVS_KEY         "config"

static const char *TAG = "netsender";

// Forward Declarations.

// extract_json gets a string or integer value from JSON.
// NB: This is NOT a general-purpose JSON parser.
bool extract_json(const std::string& json, const char* name, std::string& value);

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
        configured = false;
    } else {
        configured = true;
    }
}

esp_err_t Netsender::read_nvs_config()
{
    // Create a new handle to access non-volatile storage (NVS).
    nvs_handle_t nvs_handle;

    // Open the nvs and attach to the handle.
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Read the config from the handle.
    size_t config_size = sizeof(netsender_configuration_t);
    err = nvs_get_blob(nvs_handle, CONFIG_NVS_KEY, &config, &config_size);
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
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Write the config to the handle.
    size_t config_size = sizeof(netsender_configuration_t);
    err = nvs_set_blob(nvs_handle, CONFIG_NVS_KEY, &config, config_size);
    if (err != ESP_OK) {
        return err;
    }

    // Commit the change.
    nvs_commit(nvs_handle);

    // Close the nvs.
    nvs_close(nvs_handle);

    return ESP_OK;
}

void Netsender::print_config()
{
    ESP_LOGI(TAG, "--- CONFIG ---");
    ESP_LOGI(TAG, "Netsender v%s", NETSENDER_VERSION);
    ESP_LOGI(TAG, "MAC Address: %s", mac);
    ESP_LOGI(TAG, "Configuration size: %d", sizeof(netsender_configuration_t));
    if (configured) {
        ESP_LOGI(TAG, "boot: %d", config.boot);
        ESP_LOGI(TAG, "wifi: %s", config.wifi);
        ESP_LOGI(TAG, "dkey: %s", config.dkey);
        ESP_LOGI(TAG, "monPeriod: %d", config.monPeriod);
        ESP_LOGI(TAG, "actPeriod: %d", config.actPeriod);
        ESP_LOGI(TAG, "inputs: %d", config.inputs);
        ESP_LOGI(TAG, "outputs: %d", config.outputs);
    } else {
        ESP_LOGI(TAG, "unconfigured device...");
    }
}

bool Netsender::run(int* varsum)
{
    ESP_LOGI(TAG, "--- Starting Run Cycle ---");

    return true;

}

Netsender::~Netsender() {}

bool Netsender::request(netsender_request_type_t req, netsender_pin_t* inputs, netsender_pin_t* outputs, bool* reconfig, char* reply)
{
    return true;
}

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
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }

            // NOTE: We will assume that the response is not chunked, as responses from
            // the server should be minimal.

            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data) {
                // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len) {
                    memcpy((char*)evt->user_data + output_len, evt->data, copy_len);
                }
            } else {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL) {
                    // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                    output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                    output_len = 0;
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (content_len - output_len));
                if (copy_len) {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
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

esp_err_t Netsender::fetch_config()
{
    // Local buffer for response body.
    char* resp_buf = (char*)calloc(1, MAX_HTTP_OUTPUT_BUFFER + 1);
    if (resp_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    // Create URL array.
    static const int max_url_len = 148;
    char url[max_url_len];

    // Calculate Uptime.
    int64_t ut = esp_timer_get_time() / 1000000;

    // Create request url.
    snprintf(url, max_url_len,
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
             netsender_endpoint_config,
             NETSENDER_VERSION, mac,
             ut, NETSENDER_MODE_ONLINE
            );

    // Initialise the request.
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .event_handler = http_event_handler,
        .user_data = resp_buf,
    };
    esp_http_client_handle_t http_handle = esp_http_client_init(&http_config);

    // Send the request.
    esp_err_t err = esp_http_client_perform(http_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Check the response code.
    // TODO: Handle other response codes.
    if (int status_code = esp_http_client_get_status_code(http_handle) != 200) {
        ESP_LOGE(TAG, "got non 200 status code: %d", status_code);
        return ESP_FAIL;
    }

    bool changed = false;

    // Parse the incoming config.
    std::string param;
    std::string json_resp(resp_buf);
    if (extract_json(json_resp, "mp", param) && std::stoi(param) != config.monPeriod) {
        config.monPeriod = std::stoi(param);
        ESP_LOGI(TAG, "monPeriod changed: %d", config.monPeriod);
        changed = true;
    }
    if (extract_json(json_resp, "ap", param) && std::stoi(param) != config.actPeriod) {
        config.actPeriod = std::stoi(param);
        ESP_LOGI(TAG, "actPeriod changed: %d", config.actPeriod);
        changed = true;
    }
    if (extract_json(json_resp, "wi", param) && param != config.wifi) {
        pad_copy(config.wifi, param.c_str(), NETSENDER_WIFI_SIZE);
        ESP_LOGI(TAG, "wifi changed: %d", config.wifi);
        changed = true;
    }
    if (extract_json(json_resp, "dk", param) && param != config.dkey) {
        pad_copy(config.dkey, param.c_str(), CONFIG_NETSENDER_DKEY_SIZE);
        ESP_LOGI(TAG, "dkey changed: %d", config.dkey);
        changed = true;
    }
    if (extract_json(json_resp, "ip", param) && param != config.inputs) {
        if (check_pins(param.c_str()) >= 0) {
            pad_copy(config.inputs, param.c_str(), NETSENDER_IO_SIZE);
            ESP_LOGI(TAG, "inputs changed: %d", config.inputs);
            changed = true;
        } else {
            ESP_LOGW(TAG, "invalid inputs: %s", param.c_str());
        }
    }
    if (extract_json(json_resp, "op", param) && param != config.outputs) {
        if (check_pins(param.c_str()) >= 0) {
            pad_copy(config.outputs, param.c_str(), NETSENDER_IO_SIZE);
            ESP_LOGI(TAG, "outputs changed: %d", config.outputs);
            changed = true;
        } else {
            ESP_LOGW(TAG, "invalid outputs: %s", param.c_str());
        }
    }

    if (changed) {
        configured = true;
        write_nvs_config();
        print_config();
    }

    {
        esp_http_client_cleanup(http_handle);
    }
    free(resp_buf);

    return ESP_OK;
}

bool extract_json(const std::string& json, const char* name, std::string& value)
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
