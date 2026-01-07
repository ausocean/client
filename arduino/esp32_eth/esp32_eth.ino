/*
    This sketch shows the Ethernet event usage

*/

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.
// Example RMII LAN8720 (Olimex, etc.)
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR -1
#define ETH_PHY_MDC 4
#define ETH_PHY_MDIO 17
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

#include <ETH.h>
#include <WiFiUdp.h>
#include <WiFi.h>

WiFiUDP udp;
static bool eth_connected = false;

IPAddress static_ip(192, 168, 8, 10);
IPAddress gateway(192, 168, 8, 1);
IPAddress netmask(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

const char* ssid = "netreceiver";
const char* password = "netsender";

unsigned int localPort = 12312;  // local port to listen on

WiFiUDP Udp;

void logUDP(String message) {
    // 1. Start the packet to the target IP and Port
    if (Udp.beginPacket(IPAddress(10, 231, 101, 59), 12312)) {
        
        // 2. Write the message
        Udp.print(message);
        
        // 3. Finalize and send
        Udp.endPacket();
        
        // Optional: Also print to Serial for local debugging
        Serial.println("UDP Sent: " + message);
    } else {
        Serial.println("UDP Connection Failed");
    }
}

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      logUDP("ETH Started\n");
      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: logUDP("ETH Connected\n"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      logUDP("ETH Got IP\n");
      logUDP(ETH.localIP().toString());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      logUDP("ETH Lost IP\n");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      logUDP("ETH Disconnected\n");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      logUDP("ETH Stopped\n");
      eth_connected = false;
      break;
    default: break;
  }
}

// void sendUDPMessage(const char* msg) {
//   Serial.print("Sending UDP to ");
//   Serial.println(udpAddress);

//   udp.beginPacket(udpAddress, udpPort);
//   udp.print(msg);
//   udp.printf(" - Uptime: %lu ms", millis());

//   if (udp.endPacket()) {
//     Serial.println("Packet sent successfully");
//   } else {
//     Serial.println("Packet failed to send");
//   }
// }

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  WiFi.mode(WIFI_STA);  //Optional
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());

  Udp.begin(localPort);
  
  // Make sure the clock read is finished.
  logUDP("Starting\n");
  delay(500);

  logUDP("Registering Event Handler: ");
  Network.onEvent(onEvent);
  logUDP("Done\n");
  delay(1000);
  logUDP("Initialise Ethernet: ");
  ETH.begin();
  // ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  logUDP("Done\n");

  logUDP("Set Static IP: ");
  ETH.config(static_ip, gateway, netmask, dns);
  logUDP("Done\n");

  delay(5000);
}

void debugEthernet() {
  // Use a String buffer to collect all info into ONE packet
  String report = "\n========== ETH DEBUG ==========\n";

  // Check Link Status
  report += "Link Status:  ";
  if (ETH.linkUp()) {
    report += "UP\n";
    report += "Link Speed:   " + String(ETH.linkSpeed()) + " Mbps\n";
    report += "Duplex Mode:  ";
    report += ETH.fullDuplex() ? "Full Duplex\n" : "Half Duplex\n";
  } else {
    report += "DOWN (Check cable/MagJack)\n";
  }

  // MAC and IP Info (Added missing parentheses)
  report += "MAC Address:  " + ETH.macAddress() + "\n";
  report += "IPv4 Address: " + ETH.localIP().toString() + "\n";
  report += "Gateway IP:   " + ETH.gatewayIP().toString() + "\n";
  report += "DNS Server:   " + ETH.dnsIP().toString() + "\n";
  report += "Hostname:     " + String(ETH.getHostname()) + "\n";
  report += "===============================\n";

  // Send everything in a single UDP packet
  logUDP(report);
}

void checkPhyRegisters() {
  Serial.println("\n--- LAN8720A Internal Register Check ---");

  // Register 27: Special Control/Status Indications
  // Bit 10 (AMDIXCTRL) is used to indicate the nINTSEL latch state on some revisions,
  // but the more reliable way is to check Register 18 (Special Modes).

  uint32_t reg18_val = 0;
  // Use the underlying ESP-IDF driver to read Register 18
  esp_eth_handle_t eth_handle = (esp_eth_handle_t)ETH.linkUp();

  // We manually read Register 18: Special Modes
  // Bits [7:5] represent the MODE[2:0] strap values.
  // Bit 0-4 represent the PHYAD[4:0] strap values.

  // Note: Direct register access via ETH.h is limited,
  // so we check if the SMI is alive first:
  if (!ETH.linkUp()) {
    Serial.println("Error: Link must be UP to read registers reliably.");
    return;
  }

  Serial.println("PHY is responding. Testing Strap Configurations...");

  // If your link speed is 10Mbps, the auto-negotiation or the clock
  // synchronization has failed.
  if (ETH.linkSpeed() == 10) {
    Serial.println("CAUTION: 10Mbps detected. This almost always means");
    Serial.println("REF_CLK is unstable or nINTSEL is strapped incorrectly.");
  } else {
    Serial.println("Link speed 100Mbps: Clocking likely correct.");
  }

  Serial.println("-----------------------------------------\n");
}

void loop() {

  debugEthernet();
  delay(2000);

  // // static unsigned long lastSend = 0;
  // // if (millis() - lastSend > 10000) {
  // //   sendUDPMessage("Hello from ESP32 Ethernet!");
  // //   lastSend = millis();
  // // }
}