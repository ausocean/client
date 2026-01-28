#include "netsender.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "nvs.h"

#define MAX_HTTP_RECV_BUFFER   512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define STORAGE_NAMESPACE      "netsender"
#define CONFIG_NVS_KEY         "config"

static const char *TAG = "netsender";

Netsender::Netsender()
{
    esp_err_t err = read_nvs_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to read configuration from EEPROM");
        configured = false;
    } else {
        configured = true;
    }

    ESP_LOGI(TAG, "Netsender constructed");
}

esp_err_t Netsender::read_nvs_config(netsender_configuration_t* config)
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

void Netsender::print_config()
{
    // Get Ethernet MAC.
    uint8_t mac[6];
    esp_mac_type_t mac_type = ESP_MAC_ETH;
    esp_err_t err = esp_read_mac(mac, mac_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to read Ethernet MAC: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "--- CONFIG ---");
    ESP_LOGI(TAG, "Netsender v%s", NETSENDER_VERSION);
    ESP_LOGI(TAG, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
