#include "victron_ble.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <sys/_types.h>

#include "esp_err.h"
#include "esp_log.h"

#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

namespace {
portMUX_TYPE data_mux = portMUX_INITIALIZER_UNLOCKED;
victron_solar_data_t latest_data{};
int64_t last_update_us = 0; // 0 = no data received yet
} // namespace

// MAC for the Victron Solar Charge Controller to read. (reverse order - little endian)
constexpr uint8_t MAC[6] = {0xD9, 0x9F, 0xD7, 0x6A, 0xF3, 0xF6};

// Encryption key for the Victron Solar Charge Controller.
const uint8_t VICTRON_KEY[16] = {
    0x0c, 0x0e, 0x85, 0xdf, 0xbc, 0xcf, 0x3a, 0xf6, 0x51, 0xc4, 0x4d, 0xb2, 0x84, 0x9c, 0x3d, 0xe7,
};

static constexpr auto TAG = "victron-ble";

constexpr auto VICTRON_ID = 0x02E1;

static constexpr auto MAX_AGE_US = 120000000;

void decrypt_victron_data(const uint8_t *data, size_t data_len)
{

    auto head = 0;
    auto block_len = 0;
    uint8_t ad_type = 0;
    uint16_t manufacturer = 0;
    auto data_found = false;

    uint8_t nonce_hi, nonce_lo;

    while (head < data_len) {
        block_len = data[head];
        ad_type = data[head + 1];
        if (ad_type != 0xFF) {
            // Not manufacturer data.
            head = head + block_len + 1;
            continue;
        }

        // Get manufacturer ID.
        manufacturer = (data[head + 3] << 8) + data[head + 2];
        if (manufacturer != VICTRON_ID) {
            ESP_LOGW(TAG, "expected Victron ID (%04X), got: %04X", VICTRON_ID, manufacturer);
            head = head + block_len + 1;
            break;
        }

        data_found = true;

        // Ignore product info bytes.
        head = head + 8;

        nonce_lo = data[head + 1];
        nonce_hi = data[head + 2];
        break;
    }

    if (!data_found) {
        return;
    }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, 128);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_CTR);

    psa_key_id_t key_id = 0;
    psa_status_t status = psa_import_key(&attr, VICTRON_KEY, 16, &key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %d", (int)status);
        return;
    }

    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&op, key_id, PSA_ALG_CTR);

    if (status == PSA_SUCCESS) {
        uint8_t iv[16] = {0};
        iv[1] = nonce_hi;
        iv[0] = nonce_lo;
        status = psa_cipher_set_iv(&op, iv, sizeof(iv));
    }

    uint8_t plaintext[128] = {0};

    size_t out_len = 0;
    size_t finish_len = 0;
    if (status == PSA_SUCCESS) {
        status = psa_cipher_update(&op, &data[head + 4], data_len - 10, plaintext, 24, &out_len);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&op, plaintext + out_len, 24 - out_len, &finish_len);
    }

    if (status != PSA_SUCCESS) {
        psa_cipher_abort(&op);
        ESP_LOGE(TAG, "AES-CTR decrypt failed: %d", (int)status);
    }

    psa_destroy_key(key_id);

    victron_solar_data_t victron_data = {};
    victron_data.device_state = plaintext[0];
    victron_data.charger_error = plaintext[1];
    victron_data.battery_voltage = (plaintext[3] << 8) + plaintext[2];
    victron_data.battery_current = (plaintext[5] << 8) + plaintext[4];
    victron_data.solar_yield_today = (plaintext[7] << 8) + plaintext[6];
    victron_data.solar_power = (plaintext[9] << 8) + plaintext[8];
    victron_data.load_current = ((plaintext[11] & 0x1) << 8) + plaintext[10];

    // Update cache.
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&data_mux);
    latest_data = victron_data;
    last_update_us = now;
    taskEXIT_CRITICAL(&data_mux);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }
    if (memcmp(event->disc.addr.val, MAC, 6) != 0) {
        return 0; // Ignore non-matching ads.
    }

    decrypt_victron_data(event->disc.data, event->disc.length_data);

    return 0;
}

static void ble_on_sync(void)
{
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    ble_gap_disc_params disc_params = {};
    disc_params.passive = 1; // Listen only (no scan requests)

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, ble_gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t victron_ble_init()
{
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    psa_crypto_init();

    err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

std::optional<victron_solar_data_t> victron_ble_get_data()
{
    victron_solar_data_t copy;
    int64_t timestamp;

    taskENTER_CRITICAL(&data_mux);
    copy = latest_data;
    timestamp = last_update_us;
    taskEXIT_CRITICAL(&data_mux);

    if (timestamp == 0 || (esp_timer_get_time() - timestamp) > MAX_AGE_US) {
        return std::nullopt; // Never received, or stale
    }
    return copy;
}
