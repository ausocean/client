#include <cstdint>
#include <sys/_types.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"

// MAC for the Victron Solar Charge Controller to read.
constexpr auto MAC = "d4:a4:9f:9e:41:92";

// Encryption key for the Victron Solar Charge Controller.
const uint8_t VICTRON_KEY[16] = {
    0xF0, 0xD4, 0x9C, 0xBA, 0x4A, 0xF2, 0x7C, 0xF7, 0x0B, 0xCB, 0xE9, 0xC2, 0x6F, 0x55, 0x15, 0x03,
};

static constexpr auto TAG = "victron-ble";

// decrypts a uint8_t array of data given a nonce value, using the key defined in "KEY.h"
//
// See: https://communityarchive.victronenergy.com/storage/attachments/extra-manufacturer-data-2022-12-14.pdf
// See: https://www.victronenergy.com/upload/documents/BlueSolar-HEX-protocol.pdf
void decrypt_payload(const uint8_t *data, const int data_len, const uint8_t nonce_hi, const uint8_t nonce_lo)
{
    // Clear the encryption blocks.
    ctr.clear();

    // Set the decryption key.
    ctr.setKey(VICTRON_KEY, 16);

    // Set initial value (with nonce).
    uint8_t iv[16] = {0};
    iv[0] = nonce_hi;
    iv[1] = nonce_lo;
    ctr.setIV(iv, 16);

    // Output plaintext.
    uint8_t plaintext[24] = {0};

    // Decrypt the cyphertext.
    ctr.decrypt(plaintext, data, data_len);

#ifdef VERBOSE
    Serial.print("Data: ");
    for (auto i = 0; i < data_len; i++) {
        Serial.printf("%02X ", plaintext[i]);
    }
    Serial.println();
#endif

    // Print received data.
    Serial.printf("Charger state: \t\t%02X\n", plaintext[0]);
    Serial.printf("Charger error: \t\t%02X\n", plaintext[1]);
    Serial.printf("Battery voltage: \t%d0mV\n", (plaintext[3] << 8) + plaintext[2]);
    Serial.printf("Battery current: \t%d00mA\n", (plaintext[5] << 8) + plaintext[4]);
    Serial.printf("PV Yield Today: \t%d0Wh\n", (plaintext[7] << 8) + plaintext[6]);
    Serial.printf("PV Power: \t\t%dW\n", (plaintext[9] << 8) + plaintext[8]);
    Serial.printf("Load Current: \t\t%d00mA\n", ((plaintext[11] & 0x01) << 8) + plaintext[10]);
}

esp_err_t init_victron_ble()
{
    auto err = esp_nimble_hci_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "unable to init nimBLE stack: %s", esp_err_to_name(err));
        return err;
    }
    // Setup Bluetooth Low Energy (BLE).
    if (!BLE.begin()) {
        Serial.println("starting Bluetooth® Low Energy module failed!");
        while (1);
    }

    // Scan for the SCC, allowing duplicates.
    if (!BLE.scanForAddress(MAC, true)) {
        Serial.println("failed to perform BLE scan");
        while (1);
    };
}

void loop()
{
    // Check if there is data available on BLE connection.
    auto start_time = millis();
    while (millis() - start_time <= 10000) {
        victron = BLE.available();
        if (victron) {
            break;
        }
    }

    // Advertisement data.
    uint8_t data[512];
    if (victron.hasAdvertisementData()) {
#ifdef VERBOSE
        Serial.println("SCC has advertisement data");
#endif
        auto len = victron.advertisementDataLength();
        if (len > sizeof(data)) len = sizeof(data); // Prevent buffer overflow

        // Put ad-data into buffer.
        victron.advertisementData(data, len);

#ifdef VERBOSE
        Serial.print("Raw: ");
        for (size_t i = 0; i < len; i++) {
            Serial.printf("%02X", data[i]);
        }
        Serial.println();
#endif

        auto ad_len = data[0];
        auto ad_start = 1;
#ifdef VERBOSE
        if (data[ad_start] == 0x01) {
            Serial.println("Flags:");
            Serial.printf("\t%d - LE Limited Discoverable Mode\n", data[ad_start + 1] & 0x1);
            Serial.printf("\t%d - LE General Discoverable Mode\n", (data[ad_start + 1] >> 1) & 0x1);
            Serial.printf("\t%d - BR/EDR Not Supported.\n", (data[ad_start + 1] >> 2) & 0x1);
            Serial.printf("\t%d - Simultaneous LE and BR/EDR to Same Device Capable (Controller).\n",
                          (data[ad_start + 1] >> 3) & 0x1);
            Serial.printf("\t%d - Previously Used\n", (data[ad_start + 1] >> 4) & 0x1);
        } else {
            Serial.printf("Expected flag data type (0x01), got: %02X\n", data[ad_start]);
        }
#endif

        ad_start = ad_start + ad_len + 1;
        ad_len = data[ad_start - 1];

#ifdef VERBOSE
        Serial.printf("AD Len: %d\n", data[ad_start - 1]);
#endif
        if (data[ad_start] == 0xFF) {
#ifdef VERBOSE
            Serial.printf("Manufacturer Identifier: %02X%02X\n", data[ad_start + 2], data[ad_start + 1]);
            Serial.printf("Manufacturer Data Record type: %02X\n", data[ad_start + 3]);
            Serial.printf("Extra Bytes: %02X %02X %02X\n", data[ad_start + 4], data[ad_start + 5], data[ad_start + 6]);
            Serial.printf("Record type: %02X\n", data[ad_start + 7]);
            Serial.printf("Nonce/Count: %02X%02X\n", data[ad_start + 9], data[ad_start + 8]);
            Serial.printf("Encryption key byte 0: %02X\n", data[ad_start + 10]);
#endif

            // Decrypt data.
            decrypt_payload(&data[ad_start + 11], ad_len - 10, data[ad_start + 8], data[ad_start + 9]);

            Serial.println();
        } else {
            Serial.printf("Expected manufacturer specific data type (0xFF), got: %02X\n", data[ad_start]);
        }
        Serial.println();
    }

    delay(5000);
}
