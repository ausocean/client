/*
    This sketch shows the Ethernet event usage

*/

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.
// Example RMII LAN8720 (Olimex, etc.)
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 0
#define ETH_PHY_MDC 4
#define ETH_PHY_MDIO 17
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

#include <ETH.h>
#include <WiFiUdp.h>

WiFiUDP udp;
static bool eth_connected = false;

IPAddress static_ip(192, 168, 8, 10);
IPAddress gateway(192, 168, 8, 1);
IPAddress netmask(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

const char * udpAddress = "192.168.8.132";
const int udpPort = 12345;

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: Serial.println("ETH Connected"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH Got IP");
      Serial.println(ETH);
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("ETH Lost IP");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default: break;
  }
}

void sendUDPMessage(const char* msg) {
  Serial.print("Sending UDP to ");
  Serial.println(udpAddress);

  udp.beginPacket(udpAddress, udpPort);
  udp.print(msg);
  udp.printf(" - Uptime: %lu ms", millis());

  if (udp.endPacket()) {
    Serial.println("Packet sent successfully");
  } else {
    Serial.println("Packet failed to send");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.print("Registering Event Handler: ");
  Network.onEvent(onEvent);
  Serial.println("Done");
  delay(1000);
  Serial.print("Initialise Ethernet: ");
  ETH.begin();
  Serial.println("Done");

  Serial.print("Set Static IP: ");
  ETH.config(static_ip, gateway, netmask, dns);
  Serial.println("Done");

  delay(5000);
}

void loop() {
  if (eth_connected) {
    sendUDPMessage("Hello from ESP32 Ethernet!");
  }
  delay(10000);
}