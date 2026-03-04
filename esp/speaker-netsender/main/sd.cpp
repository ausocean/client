/*
  Name:
    sd.cpp - Functions for managing the SD card.

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

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sd_protocol_types.h"
#include "include/globals.h"

void init_sd()
{
    sdmmc_card_t *card;

    // Use the default host.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Configure the SPI bus to use the config values.
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = (gpio_num_t)CONFIG_SD_MOSI;
    bus_cfg.miso_io_num = (gpio_num_t)CONFIG_SD_MISO;
    bus_cfg.sclk_io_num = (gpio_num_t)CONFIG_SD_CLK;
    bus_cfg.quadwp_io_num = CONFIG_SD_QUADWP;
    bus_cfg.quadhd_io_num = CONFIG_SD_QUADHD;
    bus_cfg.max_transfer_sz = CONFIG_SD_MAX_TRANSFER_SZ;

    // Initialise the SPI Bus.
    ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    // Configure the SD Slot.
    sdspi_dev_handle_t sd_handle;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CS);
    slot_config.gpio_cd = static_cast<gpio_num_t>(CONFIG_SD_DET);
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    // Initialise the device.
    ESP_ERROR_CHECK(sdspi_host_init_device(&slot_config, &sd_handle));

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 32 * 1024;

    // Mount the filesystem.
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card));
}
