#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ethernet_init.h>
#include <esp_eth.h>
#include <esp_netif.h>
#include <esp_types.h>
#include "esp_event.h"
#include <sdkconfig.h>
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

#define SPEAKER_VERSION "0.0.1"

// Mount point for the SD card filesystem.
#define MOUNT_POINT "/sdcard"

static const char* TAG = "speaker";

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

/** Initialisation of the ethernet MAC, PHY, and IP/TCP */
static void init_ethernet() {
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();                      // apply default common MAC configuration
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG(); // apply default vendor-specific MAC configuration
    esp32_emac_config.smi_gpio.mdc_num = CONFIG_ETHERNET_MDC_GPIO;            // alter the GPIO used for MDC signal
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_ETHERNET_MDIO_GPIO;          // alter the GPIO used for MDIO signal
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config); // create MAC instance

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();      // apply default PHY configuration
    phy_config.phy_addr = CONFIG_ETHERNET_PHY_ADDR;           // alter the PHY address according to your board design
    phy_config.reset_gpio_num = CONFIG_ETHERNET_PHY_RST_GPIO; // alter the GPIO used for PHY reset
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy); // apply default driver configuration
    esp_eth_handle_t eth_handle = NULL; // after the driver is installed, we will get the handle of the driver
    esp_eth_driver_install(&config, &eth_handle); // install driver

    esp_event_loop_create_default(); // create a default event loop that runs in the background
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL); // register Ethernet event handler (to deal with user-specific stuff when events like link up/down happened)

    esp_netif_init(); // Initialize TCP/IP network interface (should be called only once in application)
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // apply default network interface configuration for Ethernet
    esp_netif_t *eth_netif = esp_netif_new(&cfg); // create network interface for Ethernet driver

    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)); // attach Ethernet driver to TCP/IP stack
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL); // register user defined IP event handlers
    esp_eth_start(eth_handle); // start Ethernet driver state machine
}

static void init_sd() {
    // Use the default host.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Configure the SPI bus to use the config values.
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SD_MOSI,
        .miso_io_num = CONFIG_SD_MISO,
        .sclk_io_num = CONFIG_SD_CLK,
        .quadwp_io_num = CONFIG_SD_QUADWP,
        .quadhd_io_num = CONFIG_SD_QUADHD,
        .max_transfer_sz = CONFIG_SD_MAX_TRANSFER_SZ,
    };

    // Initialise the SPI Bus.
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA));

    // Configure the SD Slot.
    sdspi_dev_handle_t sd_handle;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_CS;
    slot_config.gpio_cd = CONFIG_SD_DET;
    slot_config.host_id = host.slot;

    // Initialise the 
    ESP_ERROR_CHECK(sdspi_host_init_device(&slot_config, &sd_handle));

    ESP_LOGI(TAG, "Mounting filesystem");
    

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card));

    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Speaker Netsender Version: %s", SPEAKER_VERSION);

    ESP_LOGI(TAG, "Initialising ethernet");
    init_ethernet();
    ESP_LOGI(TAG, "Ethernet initialised");

    ESP_LOGI(TAG, "Initialising SD card");
    init_sd();
}
