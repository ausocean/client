/*
  Author:
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

#include <ArduinoBLE.h>
#include <cstdint>
#include <sys/_types.h>
#include <AES.h>
#include <CTR.h>
#include "KEY.h"

// Uncomment to see verbose comments.
// #define VERBOSE

CTR<AES128> ctr;

// MAC for the Victron Solar Charge Controller to read.
constexpr auto MAC = "f6:f3:6a:d7:9f:d9";

// Device instance to read.
BLEDevice victron;

// decrypts a uint8_t array of data given a nonce value, using the key defined in "KEY.h"
//
// See: https://communityarchive.victronenergy.com/storage/attachments/extra-manufacturer-data-2022-12-14.pdf
// See: https://www.victronenergy.com/upload/documents/BlueSolar-HEX-protocol.pdf
void decrypt_payload(const uint8_t* data, const int data_len, const uint8_t nonce_hi, const uint8_t nonce_lo) {
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
  for (auto i = 0; i < data_len; i++){
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
  Serial.printf("Load Current: \t\t%d00mA\n", ((plaintext[11]&0x01) << 8) + plaintext[10]);
}

void setup() {
  // Setup serial for logging.
  Serial.begin(115200);
  delay(100);
  Serial.println("Initialised Serial");

  // Setup Bluetooth Low Energy (BLE).
  if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy module failed!");
    while (1);
  }

  // Scan for the SCC, allowing duplicates.
  if (!BLE.scanForAddress(MAC, true)) {
    Serial.println("failed to perform BLE scan");
    while(1);
  };
}

void loop() {
  // Check if there is data available on BLE connection.
  auto start_time = millis();
  while (millis()-start_time <= 10000) {
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
      Serial.printf("\t%d - LE Limited Discoverable Mode\n", data[ad_start+1] & 0x1);
      Serial.printf("\t%d - LE General Discoverable Mode\n", (data[ad_start+1] >> 1) & 0x1);
      Serial.printf("\t%d - BR/EDR Not Supported.\n", (data[ad_start+1] >> 2) & 0x1);
      Serial.printf("\t%d - Simultaneous LE and BR/EDR to Same Device Capable (Controller).\n", (data[ad_start+1] >> 3) & 0x1);
      Serial.printf("\t%d - Previously Used\n", (data[ad_start+1] >> 4) & 0x1);
    } else {
      Serial.printf("Expected flag data type (0x01), got: %02X\n", data[ad_start]);
    }
    #endif

    ad_start = ad_start+ad_len+1;
    ad_len = data[ad_start - 1];

    #ifdef VERBOSE
    Serial.printf("AD Len: %d\n", data[ad_start - 1]);
    #endif
    if (data[ad_start] == 0xFF) {
      #ifdef VERBOSE
      Serial.printf("Manufacturer Identifier: %02X%02X\n", data[ad_start+2], data[ad_start+1]);
      Serial.printf("Manufacturer Data Record type: %02X\n", data[ad_start+3]);
      Serial.printf("Extra Bytes: %02X %02X %02X\n", data[ad_start+4], data[ad_start+5], data[ad_start+6]);
      Serial.printf("Record type: %02X\n", data[ad_start+7]);
      Serial.printf("Nonce/Count: %02X%02X\n", data[ad_start+9], data[ad_start+8]);
      Serial.printf("Encryption key byte 0: %02X\n", data[ad_start+10]);
      #endif

      // Decrypt data.
      decrypt_payload(&data[ad_start+11], ad_len-10, data[ad_start+8], data[ad_start+9]);

      Serial.println();
    } else {
      Serial.printf("Expected manufacturer specific data type (0xFF), got: %02X\n", data[ad_start]);
    }
    Serial.println();
  }

  delay(5000);
}
