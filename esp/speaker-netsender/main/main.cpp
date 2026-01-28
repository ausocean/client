// Make the app c++ compatible.
#include "driver/i2s_types.h"
#include "soc/clk_tree_defs.h"
extern "C" {
    void app_main();
}

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
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "tas5805.hpp"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"

#define SPEAKER_VERSION "0.0.1"

// Mount point for the SD card filesystem.
static const char* mount_point = "/sdcard";

// Filepath for the audio file.
static const char* audio_file = "audio.wav";

// Tag used in logs.
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
static void init_ethernet()
{
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

static void *init_sd(sdmmc_card_t **card)
{
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

    ESP_LOGI(TAG, "Mounting filesystem");

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 32 * 1024;

    // Mount the filesystem.
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, card));
    ESP_LOGI(TAG, "Filesystem mounted");

    return card;
}

static void init_amp(TAS5805** amp)
{
    // Configure and setup I2C.
    i2c_master_bus_config_t i2c_config = {};
    i2c_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_AMP_I2C_SDA);
    i2c_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_AMP_I2C_SCL);
    i2c_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_config.glitch_ignore_cnt = 7;
    i2c_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &bus_handle));
    ESP_LOGI(TAG, "I2C Master bus created");

    // Configure I2S Channel.
    i2s_chan_handle_t* tx_handle = new i2s_chan_handle_t();
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 16;
    chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = CONFIG_AMP_I2S_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_APLL,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(CONFIG_AMP_I2S_BCLK),
            .ws   = static_cast<gpio_num_t>(CONFIG_AMP_I2S_WS),
            .dout = static_cast<gpio_num_t>(CONFIG_AMP_I2S_DOUT),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Initialise channel.
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_handle, &std_cfg));

    // Enable the channel.
    // NOTE: This MUST be done before initialising amp, as the amp requires a
    // stable clock before configuration.
    ESP_ERROR_CHECK(i2s_channel_enable(*tx_handle));
    ESP_LOGI(TAG, "I2S initialized and clocks started");

    // Create a new amplifier.
    *amp = new TAS5805(bus_handle, tx_handle);
}

void app_main(void)
{
    sdmmc_card_t* sd_card;
    TAS5805* amp;

    ESP_LOGI(TAG, "Speaker Netsender Version: %s", SPEAKER_VERSION);

    ESP_LOGI(TAG, "Initialising ethernet");
    init_ethernet();
    ESP_LOGI(TAG, "Ethernet initialised");

    ESP_LOGI(TAG, "Initialising SD card");
    init_sd(&sd_card);
    ESP_LOGI(TAG, "SD initialised");

    ESP_LOGI(TAG, "Initialising I2S Amp");
    init_amp(&amp);
    ESP_LOGI(TAG, "Amp Initialised");

    // Construct the full path: /sdcard/audio.wav
    char file_path[64];
    snprintf(file_path, sizeof(file_path), "%s/%s", mount_point, audio_file);

    // Play audio.
    amp->play(file_path);
}
