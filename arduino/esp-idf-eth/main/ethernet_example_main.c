/* Ethernet Basic Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_netif.h"
#include "ethernet_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "driver/gpio.h"

static const char *TAG = "eth_example";

void invert_rmii_clock_input() {
    // EMAC_CLK_IN_IDX is the internal signal index for the RMII clock
    // GPIO_NUM_0 is your physical clock pin
    // The 'true' parameter tells the GPIO matrix to invert the signal
    esp_rom_gpio_connect_in_signal(GPIO_NUM_0, EMAC_CLK_IN_GPIO, true);
    
    printf("DEBUG: RMII Input Clock on GPIO 0 has been manually inverted in the GPIO Matrix.\n");
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  uint8_t mac_addr[6] = {0};
  /* we can get the ethernet driver handle from event data */
  esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    ESP_LOGI(TAG, "Ethernet Link Up");
    // ESP_LOGI(TAG, "Link Speed: %d", eth_handle);
    ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
             mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    eth_speed_t speed;
    esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &speed);
    ESP_LOGI(TAG, "Link Speed: %s Mbps",
             (speed == ETH_SPEED_100M) ? "100" : "10");

    // esp_eth_phy_reg_rw_data_t cmd;
    // cmd.reg_addr = 1;
    // esp_eth_ioctl(eth_handle, ETH_CMD_READ_PHY_REG, &cmd);
    // ESP_LOGI(TAG, "REG 1: %04x", *(cmd.reg_value_p));

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
                                 int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  const esp_netif_ip_info_t *ip_info = &event->ip_info;

  ESP_LOGI(TAG, "Ethernet Got IP Address");
  ESP_LOGI(TAG, "~~~~~~~~~~~");
  ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
  ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
  ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
  ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void app_main(void) {
  esp_log_level_set("esp.emac", ESP_LOG_DEBUG);
  esp_log_level_set("eth_phy", ESP_LOG_DEBUG);

  // Initialize Ethernet driver
  uint8_t eth_port_cnt = 0;
  esp_eth_handle_t *eth_handles;
  ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

  // Initialize TCP/IP network interface aka the esp-netif (should be called
  // only once in application)
  ESP_ERROR_CHECK(esp_netif_init());
  // Create default event loop that running in background
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t *eth_netifs[eth_port_cnt];
  esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];

  // Create instance(s) of esp-netif for Ethernet(s)
  if (eth_port_cnt == 1) {
    // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and
    // you don't need to modify default esp-netif configuration parameters.
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netifs[0] = esp_netif_new(&cfg);
    eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));
  } else {
    // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are
    // used and so you need to modify esp-netif configuration parameters for
    // each interface (name, priority, etc.).
    esp_netif_inherent_config_t esp_netif_config =
        ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {.base = &esp_netif_config,
                                  .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];
    for (int i = 0; i < eth_port_cnt; i++) {
      itoa(i, num_str, 10);
      strcat(strcpy(if_key_str, "ETH_"), num_str);
      strcat(strcpy(if_desc_str, "eth"), num_str);
      esp_netif_config.if_key = if_key_str;
      esp_netif_config.if_desc = if_desc_str;
      esp_netif_config.route_prio -= i * 5;
      eth_netifs[i] = esp_netif_new(&cfg_spi);
      eth_netif_glues[i] = esp_eth_new_netif_glue(eth_handles[i]);
      // Attach Ethernet driver to TCP/IP stack
      ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[i], eth_netif_glues[i]));
    }
  }

  // Register user defined event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             &eth_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             &got_ip_event_handler, NULL));

invert_rmii_clock_input();

  // Start Ethernet driver state machine
  for (int i = 0; i < eth_port_cnt; i++) {
    // Send soft restart request (bit 15 = 1).
    uint32_t reset_val = 0x8000; // Bit 15 is the Soft Reset bit
    esp_eth_phy_reg_rw_data_t cmd;
    cmd.reg_addr = 0;
    cmd.reg_value_p = &reset_val;
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handles[i], ETH_CMD_WRITE_PHY_REG, &cmd));

    bool auto_negotiation = false;
    // Pass the address of the variable (&), not the value itself
    ESP_ERROR_CHECK(
        esp_eth_ioctl(eth_handles[i], ETH_CMD_S_AUTONEGO, &auto_negotiation));

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
  }

#if CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S >= 0
  // For demonstration purposes, wait and then deinit Ethernet network
  vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S * 1000));
  ESP_LOGI(TAG, "stop and deinitialize Ethernet network...");
  // Stop Ethernet driver state machine and destroy netif
  for (int i = 0; i < eth_port_cnt; i++) {
    ESP_ERROR_CHECK(esp_eth_stop(eth_handles[i]));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(eth_netif_glues[i]));
    esp_netif_destroy(eth_netifs[i]);
  }
  esp_netif_deinit();
  ESP_ERROR_CHECK(example_eth_deinit(eth_handles, eth_port_cnt));
  ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               got_ip_event_handler));
  ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler));
  ESP_ERROR_CHECK(esp_event_loop_delete_default());
#endif // EXAMPLE_ETH_DEINIT_AFTER_S > 0
}
