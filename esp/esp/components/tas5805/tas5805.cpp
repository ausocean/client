#include <stdio.h>
#include <esp_log.h>
#include "tas5805.hpp"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "tas5805";

TAS5805::TAS5805(i2c_master_bus_handle_t handle) : bus_handle(handle) {
    // Initialise I2C device.
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = static_cast<uint16_t>(CONFIG_AMP_I2C_ADDRESS),
        .scl_speed_hz = CONFIG_AMP_I2C_CLOCK_SPEED,
    };

    // Add the device to the bus.
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // Configure default settings for the amplifier.
    uint8_t cmd = 0x00;
    write_reg(TAS8505_CHANGE_PAGE_REG, &cmd, 1); // Go to page 0.
    write_reg(TAS8505_CHANGE_BOOK_REG, &cmd, 1); // Go to book 0.

    // Set device to Hi-Z state before configuration.
    cmd = 0x02;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd, 1);

    /* Set device settings (1) - offset: 02h
        Bits:
          7:    0   - Reserved
          6-4:  000 - 768K (FSW_SEL)
          3:    0   - Reserved
          2:    1   - PBTL Mode (DAMP_PBTL)
          1-0:  00  - BD Modulation (DAMP_MOD)
          = 0b0000 0100 = 0x04
    */
    cmd = 0x04;
    write_reg(TAS8505_DEVICE_CTRL_1_REG, &cmd, 1);

    /* Set device analog gain - offset: 54h
        Bits:
          7-5:  000   - Reserved
          4-0:  00000 - 0dB (Max Vol) (ANA_GAIN)
          = 0b0000 0000 = 0x00
    */
    cmd = 0x00;
    write_reg(TAS8505_AGAIN_REG, &cmd, 1);

    /* Set device digital volume - offset: 4Ch
        Bits:
          7-0:  00100000   - ~30% Volume
          = 0b0010 0000 = 0x20
    */
    cmd = 0x00;
    write_reg(TAS8505_DIG_VOL_CTRL_REG, &cmd, 1);

    /* Set device settings (2) - offset: 03h
        Bits:
          7-5:  000 - Reserved
          4:    0   - Don't reset DSP (DIS_DSP)
          3:    0   - Normal Volume (MUTE)
          2:    0   - Reserved
          1-0:  11  - Play (CTRL_STATE)
          = 0b0000 0011 = 0x03
    */
    cmd = 0x03;
    write_reg(TAS8505_DEVICE_CTRL_2_REG, &cmd, 1);

    vTaskDelay(pdMS_TO_TICKS(10));
}

void TAS5805::write_reg(const int reg, const uint8_t* cmd, const int length) {
    // 1. Log the high-level intent
    ESP_LOGI(TAG, "Writing to Register: 0x%02X (Data Length: %d)", reg, length);

    // 2. Log the actual data content in hex format
    // Parameters: (Tag, Pointer to data, length)
    ESP_LOG_BUFFER_HEX(TAG, cmd, length);

    // Put the register first, and then the data.
    uint8_t data[length + 1];
    data[0] = reg;
    memcpy(&data[1], cmd, length);

    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, length+1, 50));
}