/*
  Name:
    NetSender - an Arduino library for sending measured values to the cloud and writing values from the cloud.
      
  License:
    Copyright (C) 2017-2024 The Australian Ocean Lab (AusOcean).

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

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>
//#include "nonarduino.h"

#include "NetSender.h"

namespace NetSender {

// Hardware constants.
#define ALARM_PIN              0    // GPIO pin indicating an alarm. Also controls the red LED.
#define LED_PIN                2    // GPIO pin corresponding to blue LED.
#define NUM_RELAYS             4    // Number of relays.

// Default values.
#define PEAK_VOLTAGE           845  // Default peak voltage, approximately 25.6V.
#define AUTO_RESTART           600  // Elapsed seconds (10 mins) for automatic restart after an alarm.

// Network constants.
#define SVC_URL                "http://data.cloudblue.org" // Web service.
#define DEFAULT_WIFI           "netreceiver,netsender"     // Default WiFi credentials.

#define MAC_SIZE               18    // Size of a string MAC address.
#define MAX_PATH               256   // Maximum URL path size.
#define RETRY_PERIOD           5     // Seconds between retrying after a failure.
#define WIFI_ATTEMPTS          100   // Number of WiFi attempts
#define WIFI_DELAY             100   // Milliseconds between WiFi attempts.
#define HTTP_TIMEOUT           10000 // Millisecond timeout for HTTP connection and request attempts.

// Constants:
enum bootReason {
  bootNormal = 0x00,
  bootWiFi   = 0x01,
  bootAlarm  = 0x02,
  bootClear  = 0x03,
};

enum httpStatusCode {
  httpOK                = 200,
  httpMovedPermanently  = 301,
  httpMovedTemporarily  = 302,
  httpSeeOther          = 303,
  httpTemporaryRedirect = 307,
  httpPermanentRedirect = 308,
};

// Service response codes.
enum rcCode {
  rcOK      = 0,
  rcUpdate  = 1,
  rcReboot  = 2,
  rcDebug   = 3,
  rcUpgrade = 4,
  rcAlarm   = 5,
  rcTest    = 6
};

// Persistent variables (stored in EEPROM as part of configuration).
// NB: Keep indexes in sync with names.
enum pvIndex {
  pvPulses,
  pvPulseWidth,
  pvPulseDutyCycle,
  pvPulseCycle,
  pvAutoRestart,
  pvAlarmPeriod,
  pvAlarmNetwork,
  pvAlarmVoltage,
  pvAlarmRecoveryVoltage,
  pvPeakVoltage,
};

const char* PvNames[] = {
  "Pulses",
  "PulseWidth",
  "PulseDutyCycle",
  "PulseCycle",
  "AutoRestart",
  "AlarmPeriod",
  "AlarmNetwork",
  "AlarmVoltage",
  "AlarmRecoveryVoltage",
  "PeakVoltage"
};

// X pins
enum xIndex {
  xSizeBW,
  xDownBW,
  xUpBW,
  x3,
  x4,
  x5,
  x6,
  x7,
  x8,
  x9,
  xA0,
  xAlarmed,
  xAlarms,
  xBoot,
  xPulseSuppress,
  xMax
};

// PowerPin describes a power pin, i.e., a pin controlling a relay.
typedef struct {
  int pin;          // GPIO pin connected to the relay.
  const char *var;  // Boolean variable that actuates the relay.
  bool alarm;       // True for the alarm pin, which is ON by default, not OFF.
} PowerPin;

// Power pins.
// NB: Update this array if the controller board is revised.
PowerPin PowerPins[] = {
  {ALARM_PIN, "Alarm",  true},
  {16,        "Power1", false},
  {14,        "Power2", false},
  {15,        "Power3", false},
};

// Variable types, which include both persistent vars and vars associated with power pins. PulseSuppress is included for convenience.
const char* VarTypes = "{\"Pulses\":\"uint\", \"PulseWidth\":\"uint\", \"PulseDutyCycle\":\"uint\", \"PulseCycle\":\"uint\", \"AutoRestart\":\"uint\", \"AlarmPeriod\":\"uint\", \"AlarmNetwork\":\"uint\", \"AlarmVoltage\":\"uint\", \"AlarmRecoveryVoltage\":\"uint\", \"PeakVoltage\":\"uint\", \"Alarm\":\"bool\", \"Power1\":\"bool\", \"Power2\":\"bool\", \"Power3\":\"bool\", \"PulseSuppress\":\"bool\"}";

// Exported globals.
Configuration Config;
ReaderFunc ExternalReader = NULL;
ReaderFunc BinaryReader = NULL;
int VarSum = 0;
bool Debug = false;

// Other globals.
static int XPin[xMax] = {100000, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0};
static bool Configured = false;
static char MacAddress[MAC_SIZE];
static IPAddress LocalAddress;
static unsigned long Time = 0;
static unsigned long AlarmedTime = 0;
static int NetworkFailures = 0;
static int SimulatedA0 = 0;

// Forward declarations.
void restart(bootReason, bool);

// Utilities:

// padcopy copies a string, padding with null characters
void padCopy(char * dst, const char * src, size_t size) {
  int ii = 0;
  for (; ii < size - 1 && ii < strlen(src); ii++) {
    dst[ii] = src[ii];
  }
  for (; ii < size; ii++) {
    dst[ii]  = '\0';
  }
}

// fmtMacAddress formats a MAC address.
char * fmtMacAddress(byte mac[6], char str[MAC_SIZE]) {
  const char* hexDigits = "0123456789ABCDEF";
  char * strp = str;
  for (int ii = 0; ii < 6; ii++) {
    *strp++ = hexDigits[(mac[ii] & 0x00F0) >> 4];
    *strp++ = hexDigits[mac[ii] & 0x000F];
    if (ii < 5) *strp++ = ':';
  }
  *strp = '\0';
  return str;
}

// extractJson gets a string or integer value from JSON.
// NB: This is NOT a general-purpose JSON parser.
bool extractJson(String json, const char * name, String& value) {
  int finish, start = json.indexOf(String("\"") + String(name) + "\"");
  if (start == -1) return false;
  start += strlen(name) + 3; // skip quotes and colon
  while (json.charAt(start) == ' ') {
    start++;
  }
  switch (json.charAt(start)) {
  case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
    finish = json.indexOf(',', start);
    break;
  case '"':
    finish = json.indexOf('"', ++start);
    break;
  default:
    return false;
  }
  if (finish == -1) finish = json.length();
  value = json.substring(start, finish);
  return true;
}

// longDelay is currently just a wrapper for delay, with a warning if WiFi is connected.
void longDelay(unsigned long ms) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("Warning: longDelay called while WiFi is connected."));
  }
  delay(ms);
}

// Initializing and reading/writing pins:

// getPowerPin returns the power pin for the given pin number, else NULL.
PowerPin * getPowerPin(int pin) {
  for (int ii = 0; ii < NUM_RELAYS; ii++) {
    if (PowerPins[ii].pin == pin) {
      return &PowerPins[ii];
    }
  }
  return NULL;
}

// setPins sets pin names in the Pin array from comma-separated names, clears unused pins and returns the size in use.
int setPins(const char * names, Pin * pins) {
  char *start = (char *)names;
  int ii = 0;
  for (; ii < MAX_PINS; ii++) {
    char * finish = strchr(start, ',');
    if (finish == NULL) {
      strcpy(pins[ii].name, start);
      ++ii;
      break;
    }
    strncpy(pins[ii].name, start, finish-start);
    pins[ii].name[finish-start] = '\0';
    start = finish + 1;
  }
  int sz = ii;
  for (; ii < MAX_PINS; ii++) {
    pins[ii].name[0] = '\0';
  }
  return sz;
}

// resetPowerPins resets all power pins.
void resetPowerPins() {
  for (int ii = 0; ii < NUM_RELAYS; ii++) {
    if (PowerPins[ii].alarm) {
      continue;
    }
    pinMode(PowerPins[ii].pin, OUTPUT);
    digitalWrite(PowerPins[ii].pin, LOW);
    if (Debug) Serial.print(F("Reset power pin: D")), Serial.println(PowerPins[ii].pin);
  }
}

// initPins initializes digital pins. On startup, power pins are also initialized.
void initPins(bool startup) {
  Pin pins[MAX_PINS];

  for (int ii = 0, sz = setPins(Config.inputs, pins); ii < sz; ii++) {
    if (pins[ii].name[0] == 'D') {
      int pn = atoi(pins[ii].name + 1);
      pinMode(pn, INPUT);
    }
  }

  for (int ii = 0, sz = setPins(Config.outputs, pins); ii < sz; ii++) {
    if (pins[ii].name[0] == 'D') {
      int pn = atoi(pins[ii].name + 1);
      pinMode(pn, OUTPUT);
    }
  }

  if (startup) {
    pinMode(ALARM_PIN, OUTPUT);
    digitalWrite(ALARM_PIN, HIGH);
    resetPowerPins();
  }
}

// readPin reads a pin value and returns it, or -1 upon error.
// The data field will be set in the case of binary data, otherwise it will be NULL.
// When SimulatedA0 is non-zero, this value is returned as the value for A0 one time only.
// The following call to read A0 will therefore always return the actual value.
int readPin(Pin * pin) {
  int pn = atoi(pin->name + 1);
  pin->value = -1;
  pin->data = NULL;
  switch (pin->name[0]) {
  case 'A':
    if (pn == 0 && SimulatedA0 != 0) {
      if (Debug) Serial.println(F("Simulating A0"));
      pin->value = SimulatedA0;
      SimulatedA0 = 0;
    } else {
      pin->value = analogRead(pn);
    }
    break;
  case 'B':
    if (BinaryReader != NULL) {
      pin->value = (*BinaryReader)(pin);
    }
    break;
  case 'D':
    pin->value = digitalRead(pn);
    break;
  case 'X':
    if (pn >= 0 && pn < xMax) {
      pin->value = XPin[pn];
    } else if (ExternalReader != NULL) {
      pin->value = (*ExternalReader)(pin);
    }
    break;
  }
  if (Debug) Serial.print(F("Read ")), Serial.print(pin->name), Serial.print(F("=")), Serial.println(pin->value);
  return pin->value;
}

// setAlarmTimer sets/resets the alarm timer.
void setAlarmTimer(bool alarm) {
  if (alarm) {
    if (AlarmedTime == 0) {
      AlarmedTime = millis();
      if (Debug) Serial.println(F("Alarm timer ON"));
    } else {
      if (Debug) Serial.println(F("Alarm timer continuing"));
    }
  } else {
    if (Debug) Serial.println(F("Alarm timer OFF"));
    AlarmedTime = 0;
  }
}

// writePin writes a pin, with writes to the alarm pin stopping/starting the alarm timer.
void writePin(Pin * pin) {
  int pn = atoi(pin->name + 1);
  PowerPin * pp;
  if (Debug) Serial.print(F("Write ")), Serial.print(pin->name), Serial.print(F("=")), Serial.println(pin->value);
  switch (pin->name[0]) {
  case 'A':
    analogWrite(pn, pin->value);
    break;
  case 'D':
    pp = getPowerPin(pn);
    if (pp != NULL && pp->alarm) {
      // Set/reset the alarm timer when writing the alarm pin.
      setAlarmTimer(!pin->value);
    }
    digitalWrite(pn, pin->value);
    break;
  case 'X':
    switch (pn) {
    case xA0:
      SimulatedA0 = pin->value;
      if (Debug) Serial.print(F("Set simulated value for AO: ")), Serial.println(pin->value);
      break;
    case xPulseSuppress:
      if (pin->value == 1) {
        XPin[xPulseSuppress] = 1;
      }
      break;
    }
    break;
  default:
    if (Debug) Serial.println(F("Warning: Invalid write"));
  }
}

// pulsePin generates pulses on the given pin, with each pulse having
// the given width (seconds) and duty cycle (%), with the latter defaulting
// to 50. When the dutyCycle is greater than 100, we subtract 100 and
// pulse from HIGH to LOW instead of LOW to HIGH. In pulse suppression
// true, the equivalent delay is produced without actual pulses being
// generated.
void pulsePin(int pin, int pulses, int width, int dutyCycle=50) {
  int level = LOW;
  if (pulses <= 0) return;
  if (width <= 0 || pulses * width > Config.monPeriod) return;
  if (dutyCycle < 0 || dutyCycle > 200 ) return;
  if (Debug) {
    if (XPin[xPulseSuppress]) {
      Serial.print(F("Pulse suppressed: ")), Serial.print(pulses * width), Serial.println(F("s"));
    } else {
      Serial.print(F("Pulsing ")), Serial.print(pulses), Serial.print(F(",")), Serial.print(width), Serial.print(F(",")), Serial.println(dutyCycle);
    }
  }
  if (dutyCycle == 0) {
    dutyCycle = 50;
  }
  if (dutyCycle > 100) {
    dutyCycle = dutyCycle - 100;
    level = HIGH;
  }
  width *= 1000; // in milliseconds
  int active = width * dutyCycle / 100;
  int timing[2] = {active, width - active};
  for (int ii = 0; ii < pulses * 2; ii++) {
    if (!XPin[xPulseSuppress]) {
      digitalWrite(pin, ii % 2 ? level : !level);
    }
    delay(timing[ii % 2]);
  }
}

// cyclePin cycles a digital pin on and off, unless in pulse mode.
void cyclePin(int pin, int cycles, bool force) {
  if (!force && Config.vars[pvPulses] != 0) return;
  pulsePin(pin, cycles, 1, 150);
}

// EEPROM utilities:

// readConfig reads the configuration from EEPROM.
void readConfig(Configuration* config) {
  unsigned char *bytep = (unsigned char *)config;
  EEPROM.begin(sizeof(Configuration));
  for (int ii = 0; ii < sizeof(Configuration); ii++) {
    unsigned char ch = EEPROM.read(ii);
    if (ch == 255) {
      *bytep++ = '\0';
    } else {
      *bytep++ = ch;
    }
  }
  if (config->version/10 != VERSION/10) {
    if (Debug) Serial.print(F("Clearing config with version ")), Serial.println(config->version);
    memset((unsigned char *)config, 0, sizeof(Configuration));
    config->version = VERSION;
  }
  if (config->monPeriod == 0) {
    config->monPeriod = RETRY_PERIOD;
  }
}

// printConfig prints our MAC address and current configuration.
void printConfig() {
  Serial.print(F("NetSender v")), Serial.println(VERSION);
  Serial.print(F("MAC address: ")), Serial.println(MacAddress);
  Serial.print(F("version: ")), Serial.println(Config.version);
  Serial.print(F("boot: ")), Serial.println(Config.boot);
  Serial.print(F("wifi: ")), Serial.println(Config.wifi);
  Serial.print(F("dkey: ")), Serial.println(Config.dkey);
  Serial.print(F("monPeriod: ")), Serial.println(Config.monPeriod);
  Serial.print(F("actPeriod: ")), Serial.println(Config.actPeriod);
  Serial.print(F("inputs: ")), Serial.println(Config.inputs);
  Serial.print(F("outputs: ")), Serial.println(Config.outputs);
  for (int ii = 0; ii < MAX_VARS; ii++) {
    Serial.print(PvNames[ii]), Serial.print(F("=")), Serial.println(Config.vars[ii]);
  }
  Serial.flush();
}

// writeConfig writes the configuration to EEPROM.
void writeConfig(Configuration* config) {
  unsigned char *bytep = (unsigned char *)config;
  if (Debug) Serial.println(F("Writing config"));
  EEPROM.begin(sizeof(Configuration));
  for (int ii = 0; ii < sizeof(Configuration); ii++) {
    EEPROM.write(ii, *bytep++);
  }
  EEPROM.commit();
  if (Debug) Serial.println(F("Wrote config")), printConfig();
}

// writeAlarm writes the alarm pin.
// The continuous param controls the alarm duration:
//   If true, the alarm duration is continuous (until canceled by an auto restart).
//   If false, the alarm is for AlarmPeriod seconds.
//   For a continuous alarms, power pins are reset,
//   but restoring power is left up to the normal actuation cycle.
//
// When alarm is false, alarms are cleared regardless of whether a
// particular alarm is enabled or not.
//
// Side effects:
//   XPin[xAlarmed], the alarm indicator, is true if the alarm is set, false otherwise.
//   XPin[xAlarms], the alarm count, is incremented each time the alarm is set.
//   AlarmedTime is set to the alarm start time.
void writeAlarm(bool alarm, bool continuous) {
  if (!alarm) {
    if (Debug) Serial.println(F("Cleared alarm"));
    digitalWrite(ALARM_PIN, HIGH);
    XPin[xAlarmed] = false;
    AlarmedTime = 0;
    return;
  }
  if (Config.vars[pvAlarmNetwork] == 0 && Config.vars[pvAlarmVoltage] == 0) {
    return;
  }
  if (Debug) Serial.println(F("Set alarm"));
  digitalWrite(ALARM_PIN, LOW);
  XPin[xAlarms]++;

  if (continuous) {
    XPin[xAlarmed] = true;
    if (AlarmedTime == 0) {
      AlarmedTime = millis();
    }
    digitalWrite(ALARM_PIN, LOW);
    resetPowerPins();
    return;
  }

  // Alarm is temporary.
  if (Debug) Serial.print(F("Alarming for ")), Serial.print(Config.vars[pvAlarmPeriod]), Serial.println(F("s"));
  delay(Config.vars[pvAlarmPeriod] * 1000);
  if (Debug) Serial.println(F("Cleared temporary alarm"));
  digitalWrite(ALARM_PIN, HIGH);
  XPin[xAlarmed] = false;
}

// restart restarts the ESP8266, saving the reason, and raising an
// alarm before restarting when alarm is true.
void restart(bootReason reason, bool alarm) {
  if (Debug) Serial.print(F("Restarting (")), Serial.print(reason), Serial.print(F(",")), Serial.print(alarm), Serial.println(F(")"));
  if (reason != Config.boot) {
    if (Debug) Serial.print(F("Writing boot reason: ")), Serial.println(reason);
    Config.boot = reason;
    writeConfig(&Config);
  }
  if (alarm) {
    writeAlarm(true, true);
    delay(2000);
  }
  cyclePin(LED_PIN, 6, true);
  ESP.restart();
}

// wifiOn and wifiOff are low-level replacements for
// WiFi.forceSleepWake() and WiFi.forceSleepBegin(). The latter
// results in spurious watchdog timer resets in ESP82866 Arduino
// 2.4.0, 2.5.2 (and possibly other versions.)  See
// https://github.com/esp8266/Arduino/issues/4082.
void wifiOn() {
  wifi_fpm_do_wakeup();
  wifi_fpm_close();
  wifi_set_opmode(STATION_MODE);
  wifi_station_connect();
}

void wifiOff() {
  wifi_station_disconnect();
  bool stopped = false;
  for (int attempts = 0; !stopped && attempts < WIFI_ATTEMPTS; attempts++) {
    stopped = (wifi_station_get_connect_status() == DHCP_STOPPED);
    delay(WIFI_DELAY);
  }
  if (!stopped) {
    Serial.println("Warning: DHCP not stopping.");
    restart(bootWiFi, true);
  }
  wifi_set_opmode(NULL_MODE);
  wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);
}

// wifiControl turns the WiFi on and off, returning true on success,
// false otherwise. A failure to turn on the WiFi is regarded as a
// network failure, although it could be due to the ESP, not the
// network. A failure to turn off the WiFi however is regarded as an
// ESP failure and results in a restart.
bool wifiControl(bool on) {
  if (on) {
    if (WiFi.status() == WL_CONNECTED) {
      return true; // Nothing to do.
    }
    wifiOn();
    delay(WIFI_DELAY);
    if (!WiFi.mode(WIFI_STA)) {
      Serial.println(F("Warning: WiFi not starting"));
      return false;
    }
    if (Debug) Serial.println(F("WiFi on"));
  } else {
    if (!WiFi.mode(WIFI_STA)) {
      return true;
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect();
      delay(WIFI_DELAY);
    }
    for (int attempts = 0; WiFi.status() == WL_CONNECTED && attempts < WIFI_ATTEMPTS; attempts++) {
      delay(WIFI_DELAY);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("Warning: WiFi not disconnecting"));
      restart(bootWiFi, true);
    }
    wifiOff();
    delay(WIFI_DELAY);
    if (Debug) Serial.println(F("WiFi off"));
  }
  return true;
}

// wifiConnect attempt to connects to the supplied WiFi network.
// wifi is network info as CSV "ssid,key" (ssid must not contain a comma!)
bool wifiConnect(const char * wifi) {
  // NB: only works for WPA/WPA2 network
  char ssid[WIFI_SIZE], *key;
  if (wifi[0] == '\0') {
    return false; // though not a connection failure
  }
  strcpy(ssid, wifi);
  key = strchr(ssid, ',');
  if (key == NULL) {
    key = ssid + strlen(ssid);
  } else {
    *key++ = '\0';
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (strcmp(WiFi.SSID().c_str(), ssid) == 0) {
      return true;
    }
    WiFi.disconnect();
  }

  if (Debug) Serial.print(F("Requesting DHCP from ")), Serial.println(wifi);
  WiFi.begin(ssid, key);
  delay(WIFI_DELAY);
 
  // WiFi.waitForConnectResult can end up in an infinite loop, so don't use it!
  // NB: connecting can take several seconds, so ensure WIFI_ATTEMPTS x WIFI_DELAY is at least 5000ms.
  for (int attempts = 0; WiFi.status() != WL_CONNECTED && attempts < WIFI_ATTEMPTS; attempts++) {
    delay(WIFI_DELAY);
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (Debug) Serial.println(F("Failed to connect to WiFi"));
    return false;
  }

  LocalAddress = WiFi.localIP();
  if (Debug) Serial.print(F("Obtained DHCP IP address ")), Serial.println(LocalAddress);
  return true;
}

// wifiBegin attempts to begin a WiFi session, first, using the
// configured hotspot and, second, using the default hotspot.
bool wifiBegin() {
  bool ok = wifiControl(true);
  if (ok) {
    ok = wifiConnect(Config.wifi);
    if (!ok && strcmp(Config.wifi, DEFAULT_WIFI) != 0) {
      delay(WIFI_DELAY);
      ok = wifiConnect(DEFAULT_WIFI);
    }
  }
  return ok;
}

// httpRequest sends a request to an HTTP server and gets the response,
// returning true on success or false otherwise.
bool httpRequest(String url, String body, String& reply) {
  HTTPClient http;
  WiFiClient client;

  bool get = body.length() == 0;
  if (Debug) Serial.print(get ? F("GET ") : F("POST ")), Serial.println(url);
  http.setTimeout(HTTP_TIMEOUT);
  http.begin(client, url);
  const char* locationHeader[] = {"Location"};
  http.collectHeaders(locationHeader, 1);
  if (!get) {
    http.addHeader("Content-Type", "application/json");
  }
  auto status = get ? http.GET(): http.POST(body);

  switch (status) {
  case httpMovedPermanently:
  case httpMovedTemporarily:
  case httpSeeOther:
  case httpTemporaryRedirect:
  case httpPermanentRedirect:
    url = http.header("Location");
    if (Debug) Serial.print(F("Redirecting to: ")), Serial.println(url);
    http.end();
    return httpRequest(url, body, reply); // Redirect to the new location.
  }

  if (status == httpOK) {
    reply = http.getString();
    if (Debug) Serial.print(F("Reply: ")), Serial.println(reply);
    http.end();
    return true;
  } else {
    if (Debug) Serial.print(F("Warning: HTTP request failed with status: ")), Serial.println(status);
    http.end();
    return false;
  }
}

// Issue a single request, writing polled values to 'inputs' and actuated values to 'outputs'.
// Sets 'reconfig' true if reconfiguration is required, false otherwise.
// Side effects: 
//   Updates VarSum global when differs from the varsum ("vs") parameter.
//   Sets Configured global to false for update and alarm requests.
//   Updates, enters debug mode or alarm mode, or reboots according to the response code ("rc").
bool request(RequestType req, Pin * inputs, Pin * outputs, bool * reconfig, String& reply) {
  char path[MAX_PATH];
  String param;
  String body;
  bool ok;
  unsigned long ut = millis()/1000;
  *reconfig = false;

  switch (req) {
  case RequestConfig:
    sprintf(path, "/config?vn=%d&ma=%s&dk=%s&la=%d.%d.%d.%d&ut=%ld", VERSION, MacAddress, Config.dkey,
            LocalAddress[0], LocalAddress[1], LocalAddress[2], LocalAddress[3], ut);
    break;
  case RequestPoll:
    sprintf(path, "/poll?vn=%d&ma=%s&dk=%s&ut=%ld", VERSION, MacAddress, Config.dkey, ut);
    break;
  case RequestAct:
    sprintf(path, "/act?vn=%d&ma=%s&dk=%s&ut=%ld", VERSION, MacAddress, Config.dkey, ut);
    break;
  case RequestVars:
    sprintf(path, "/vars?vn=%d&ma=%s&dk=%s&ut=%ld", VERSION, MacAddress, Config.dkey, ut);
    break;
  }

  if (inputs != NULL) {
    for (int ii = 0; ii < MAX_PINS && inputs[ii].name[0] != '\0'; ii++) {
      if (inputs[ii].value < 0 && strcmp(inputs[ii].name, "X10") != 0) {
        // Omit negative scalars (except X10) or missing/partial binary data.
        if (Debug) Serial.print(F("Warning: Not sending ")), Serial.println(inputs[ii].name);
        continue;
      }
      sprintf(path + strlen(path), "&%s=%d", inputs[ii].name, inputs[ii].value);
      // Populate the body with binary data, if any.
      if (inputs[ii].data != NULL && inputs[ii].value > 0) {
        body += String((const char*)(inputs[ii].data));
      }
    }
  }

  if (httpRequest(String(SVC_URL)+String(path), body, reply)) {
    if (XPin[xAlarmed]) {
      writeAlarm(false, true); // Reset alarm.
    }
    NetworkFailures = 0;
  } else {
    NetworkFailures++;
    if (Debug) Serial.print(F("Network failures: ")), Serial.println(NetworkFailures);
    if (Config.vars[pvAlarmNetwork] > 0 && NetworkFailures >= Config.vars[pvAlarmNetwork]) {
      // Too many network failures; raise the alarm!
      writeAlarm(true, false);
      NetworkFailures = 0;
    }
    return false;
  }

  if (!reply.startsWith("{")) {
    if (Debug) Serial.println(F("Warning: Malformed response"));
    return false;
  }

  // Since version 138 and later, poll requests also return output values.
  if (req == RequestPoll || req == RequestAct) {
    bool found = false;
    for (int ii = 0; ii < MAX_PINS && outputs[ii].name[0] != '\0'; ii++) {
      if (extractJson(reply, outputs[ii].name, param)) {
        outputs[ii].value = param.toInt();
        writePin(&outputs[ii]);
      } else {
        outputs[ii].value = -1;
        if (Debug) Serial.print(F("Warning: Missing value for output pin ")), Serial.println(outputs[ii].name);
      }
    }
  }

  if (extractJson(reply, "rc", param)) {
    switch (param.toInt()) {
    case rcOK:
      break;
    case rcUpdate:
      if (Debug) {
        Serial.println(F("Received update request."));
      }
      *reconfig = true;
      Configured = false;
      break;
    case rcReboot:
      if (Debug) Serial.println(F("Received reboot request."));
      if (Configured) {
        // Kill the power too.
        resetPowerPins();
        restart(bootNormal, false);
      } // else ignore reboot request unless configured
      break;
    case rcDebug:
      if (!Debug) {
        Serial.println(F("Debug mode on."));
        Debug = true;
      }
      break;
    case rcAlarm:
      if (Debug) Serial.println(F("Received alarm request."));
      if (Configured && Config.vars[pvAlarmPeriod] > 0) {
        writeAlarm(true, false);
        *reconfig = true;
        Configured = false;
      }
      break;
    default:
      break;
    }
  }

  if (extractJson(reply, "vs", param)) {
    int vs = param.toInt();
    if (vs != VarSum) {
      if (Debug) Serial.println(F("Varsum changed"));
    }
    VarSum = vs;
  }

  if (extractJson(reply, "er", param)) {
    // we let the caller deal with errors
    if (Debug) Serial.print(F("Error: ")), Serial.println(param);
  }

  return true;
}

// request config, and return true upon success, or false otherwise
// Side effects:
//   Sets Configured global to true upon success.
bool config() {
  String reply, error, param;
  bool reconfig = false;
  bool changed = false;
  Pin pins[2];

  // As of v160, var types (vt) are sent with config requests.
  strcpy(pins[0].name, "vt");
  pins[0].value = strlen(VarTypes);
  pins[0].data = (byte*)VarTypes;
  pins[1].name[0] = '\0';

  if (!request(RequestConfig, pins, NULL, &reconfig, reply) || extractJson(reply, "er", param)) {
    cyclePin(LED_PIN, 2, false);
    return false;
  } 
  if (Debug) Serial.print(F("Config response: ")), Serial.println(reply);

  if (extractJson(reply, "mp", param) && param.toInt() != Config.monPeriod) {
    Config.monPeriod = param.toInt();
    if (Debug) Serial.print(F("Mon. period changed: ")), Serial.println(Config.monPeriod);
    changed = true;
  }
  if (extractJson(reply, "ap", param) && param.toInt() != Config.actPeriod) {
    Config.actPeriod = param.toInt();
    if (Debug) Serial.print(F("Act. period changed: ")), Serial.println(Config.actPeriod);
    changed = true;
  }
  if (extractJson(reply, "wi", param) && param != Config.wifi) {
    padCopy(Config.wifi, param.c_str(), WIFI_SIZE);
    if (Debug) Serial.print(F("Wifi changed: ")), Serial.println(Config.wifi);
    changed = true;
  }
  if (extractJson(reply, "dk", param) && param != Config.dkey) {
    padCopy(Config.dkey, param.c_str(), DKEY_SIZE);
    if (Debug) Serial.print(F("Dkey changed: ")), Serial.println(Config.dkey);
    changed = true;
  }
  if (extractJson(reply, "ip", param) && param != Config.inputs) {
    padCopy(Config.inputs, param.c_str(), IO_SIZE);
    if (Debug) Serial.print(F("Inputs changed: ")), Serial.println(Config.inputs);
    changed = true;
  }
  if (extractJson(reply, "op", param) && param != Config.outputs) {
    padCopy(Config.outputs, param.c_str(), IO_SIZE);
    if (Debug) Serial.print(F("Outputs changed: ")), Serial.println(Config.outputs);
    changed = true;
  }

  if (changed) {
    writeConfig(&Config);
    initPins(false); // NB: Don't re-initalize power pins.
    cyclePin(LED_PIN, 4, false);
  }
  Configured = true;
  return true;
}

// Retrieve vars from data host, return the persistent vars and
// set changed to true if a persistent var has changed.
// Transient vars, such as "id" are not saved.
// Missing persistent vars default to 0, except for peak voltage and auto restart.
bool getVars(int vars[MAX_VARS], bool* changed) {
  String reply, error, id, param;
  bool reconfig;
  *changed = false;

  if (!request(RequestVars, NULL, NULL, &reconfig, reply) || extractJson(reply, "er", param)) {
    return false;
  }
  bool hasId = extractJson(reply, "id", id);
  if (hasId && Debug) Serial.print(F("id=")), Serial.println(id);

  for  (int ii = 0; ii < MAX_VARS; ii++) {
    int val = 0;
    if (hasId) {
      String var = id + '.' + String(PvNames[ii]);
      if (extractJson(reply, var.c_str(), param)) {
        val = param.toInt();
      }
    } else {
      if (extractJson(reply, PvNames[ii], param)) {
        val = param.toInt();
      }
    }

    // Set values for variables with non-zero defaults.
    if (val == 0) {
      switch (ii) {
      case pvPeakVoltage:
        val = PEAK_VOLTAGE;
        break;
      case pvAutoRestart:
        val = AUTO_RESTART;
        break;
      }
    }
    vars[ii] = val;

    if (Debug) Serial.print(PvNames[ii]), Serial.print(F("=")), Serial.print(vars[ii]);
    if (Config.vars[ii] != val) {
      *changed = true;
      if (Debug) Serial.print(F("!=")), Serial.print(Config.vars[ii]);
     }
     Serial.println(F(""));
  }

  // Clamp alarm voltages so as to not exceed the peak voltage.
  if (vars[pvAlarmVoltage] > vars[pvPeakVoltage]) {
    vars[pvAlarmVoltage] = vars[pvPeakVoltage];
    if (Config.vars[pvAlarmVoltage] != vars[pvAlarmVoltage]) {
      *changed = true;
    }
  }
  if (vars[pvAlarmRecoveryVoltage] > vars[pvPeakVoltage]) {
    vars[pvAlarmRecoveryVoltage] = vars[pvPeakVoltage];
    if (Config.vars[pvAlarmRecoveryVoltage] != vars[pvAlarmRecoveryVoltage]) {
      *changed = true;
    }
  }

  return true;
}

// write vars
void writeVars(int vars[MAX_VARS]) {
  if (Debug) Serial.println(F("Writing vars"));
  memcpy(Config.vars, vars, sizeof(Config.vars));
  writeConfig(&Config);
}

// init should be called from setup once
void init(void) {
  // Disable WiFi persistence in flash memory.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(2000);

  // Get Config.
  readConfig(&Config);
  // Get boot info.
  XPin[xBoot] = Config.boot;
  Serial.print(F("Boot reason: ")), Serial.println(Config.boot);

  // Initialize GPIO pins, including power pins.
  initPins(true);
  // Reset alarm.
  writeAlarm(false, true);

  // Save the formatted MAC address.
  byte mac[6];
  WiFi.macAddress(mac);
  fmtMacAddress(mac, MacAddress);
}

// Pause to maintain timing accuracy, adjusting the timing lag in the process.
// Pulsed is how long we've pulsed in milliseconds, or the equivalent delay if suppressing pulses.
// If we're here because of a problem and we're not pulsing, we just pause long enough to retry,
// since timing accuracy is moot. If pulsing, we pause for the active time remaining this cycle,
// unless we're out of time.
bool pause(bool ok, unsigned long pulsed, long * lag) {
  if (!ok && pulsed == 0) {
    if (Debug) Serial.print(F("Retrying in ")), Serial.print(RETRY_PERIOD), Serial.println(F("s"));
    delay(RETRY_PERIOD * 1000L);
    return ok;
  }

  unsigned long now = millis();
  long remaining = Config.actPeriod * 1000L - pulsed;
  *lag += (now - Time - pulsed);

  if (Debug) {
    Serial.print(F("Pulsed time: ")), Serial.print(pulsed), Serial.println(F("ms"));
    Serial.print(F("Total lag: ")), Serial.print(*lag), Serial.println(F("ms"));
    Serial.print(F("Run time: ")), Serial.print(now - Time), Serial.println(F("ms"));
  }

  if (remaining > *lag) {
    remaining -= *lag;
    if (Debug) Serial.print(F("Pausing for ")), Serial.print(remaining), Serial.println(F("ms"));
    longDelay(remaining);
    *lag = 0;
  } else {
    if (Debug) Serial.println(F("Skipped pause"));
  }
  return ok;
}

// run should be called from loop until it returns true, e.g., 
//  while (!run(&varsum)) {
//    ;
//  }
// May return false while either connected to WiFi or not.
// NB: pulse suppression must be re-enabled each cycle via the X14 pin.
bool run(int* varsum) {
  Pin inputs[MAX_PINS], outputs[MAX_PINS];
  String reply;
  bool reconfig = false;
  unsigned long pulsed = 0;
  long lag = 0;
  unsigned long now = millis();
  int vars[MAX_VARS];
  bool changed;

  // Measure lag to maintain accuracy between cycles.
  if (Time > 0 && now > Time) {
    lag = (long)(now - Time) - (Config.monPeriod * 1000L);
    if (Debug) Serial.print(F("Initial lag: ")), Serial.print(lag), Serial.println(F("ms"));
    if (lag < 0) {
      lag = 0;
    }
  }
  Time = now; // record the start of each cycle

  // Have we just restarted due to an alarm?
  if (Config.boot == bootAlarm) {
    // Clear the boot state so we only perform this check once per restart.
    // NB: bootClear is a transient state that we don't write to the EEPROM.
    Config.boot = bootClear;
    // Attempt to refresh vars in case the recent alarm was due to operator error.
    if (wifiBegin() && getVars(vars, &changed)) {
      if (changed) {
        if (Debug) Serial.println(F("Persistent variable(s) changed"));
        writeVars(vars);
      }
      *varsum = VarSum;
    }
  }

  // Restart if the alarm has gone for on too long.
  // NB: Check AlarmedTime regardless of whether XPin[xAlarmed] is true or not.
  if (AlarmedTime > 0) {
    int alarmed; // Alarmed duration in seconds.
    if (now >= AlarmedTime) {
      alarmed = (now - AlarmedTime)/1000;
    } else { // rolled over
      alarmed = ((0xffffffff - AlarmedTime) + now)/1000;
    }
    if (Debug) Serial.print(F("Alarm duration: ")), Serial.print(alarmed), Serial.println(F("s"));
    if (alarmed >= Config.vars[pvAutoRestart]) {
      restart(bootAlarm, false);
    }
  }

  // If we're not configured, report our MAC address and current configuration.
  if (!Configured || Config.dkey[0] == '\0') {
    printConfig();
  }

  // Pulsing happens before anything else, regardless of network connectivity.
  if (Config.vars[pvPulses] != 0 && Config.vars[pvPulseWidth] != 0) {
    pulsePin(LED_PIN, Config.vars[pvPulses], Config.vars[pvPulseWidth], Config.vars[pvPulseDutyCycle]);
    pulsed = (unsigned long)Config.vars[pvPulses] * Config.vars[pvPulseWidth] * 1000L;
    long gap = (Config.vars[pvPulseCycle] * 1000L) - (long)pulsed;
    if (gap > 0) {
      for (int spanned = 0; spanned < Config.monPeriod - Config.vars[pvPulseCycle]; spanned += Config.vars[pvPulseCycle]) {
        if (Debug) Serial.print(F("Pulse group gap: ")), Serial.print(gap), Serial.println(F("ms"));
        longDelay(gap);
        pulsePin(LED_PIN, Config.vars[pvPulses], Config.vars[pvPulseWidth], Config.vars[pvPulseDutyCycle]);
        pulsed += (gap + ((unsigned long)Config.vars[pvPulses] * Config.vars[pvPulseWidth]));
      }
    }
  }
  XPin[xPulseSuppress] = 0;

  // Check voltage if we have an alarm voltage.
  if (Config.vars[pvAlarmVoltage] > 0) {
    Pin pin = { "A0" };
    if (Debug) Serial.println(F("Checking voltage"));
    XPin[xA0] = readPin(&pin);
    if (XPin[xA0] < Config.vars[pvAlarmVoltage]) {
      if (!XPin[xAlarmed]) {
        // low voltage; raise the alarm and turn off WiFi!
        if (Debug) Serial.println(F("Low voltage alarm!"));
        cyclePin(LED_PIN, 5, true);
        writeAlarm(true, true);
        wifiControl(false);
      }
      return pause(false, pulsed, &lag);
    }
    if (XPin[xAlarmed]) {
      if (XPin[xA0] < Config.vars[pvAlarmRecoveryVoltage]) {
        return pause(false, pulsed, &lag);
      }
      if (Debug) Serial.println(F("Low voltage alarm cleared"));
      writeAlarm(false, true);
    }
    if (XPin[xA0] > Config.vars[pvPeakVoltage]) {
      if (Debug) Serial.println(F("Warning: High voltage"));
    }
  } else {
    XPin[xA0] = -1;
    if (Debug) Serial.println(F("Skipped voltage check"));
  }

  // Read inputs, if any.
  // NB: We do this before we are connected to the network.
  for (int ii = 0, sz = setPins(Config.inputs, inputs); ii < sz; ii++) {
    readPin(&inputs[ii]);
  }

  // Turn on WiFI, connect, and then send input values and/or receive output values.
  if (!wifiBegin()) {
    NetworkFailures++;
    if (Config.vars[pvAlarmNetwork] > 0 && NetworkFailures >= Config.vars[pvAlarmNetwork]) {
      // too many network failures; raise the alarm!
      writeAlarm(true, false);
      NetworkFailures = 0;
    } else {
      cyclePin(LED_PIN, 3, false);
    }
    wifiControl(false); // No-op if WiFi is not on.
    return pause(false, pulsed, &lag);
  }
  
  // Attempt configuration whenever:
  //   (1) there are no inputs and no outputs, or 
  //   (2) we receive a update code
  if (Config.inputs[0] == '\0' && Config.outputs[0] == '\0') {
    if (!config()) {
      wifiControl(false);
      return pause(false, pulsed, &lag);
    }
  }

  // Since version 138 the poll method returns outputs as well as inputs,
  if (Config.inputs[0] != '\0') {
    setPins(Config.outputs, outputs);
    if (!request(RequestPoll, inputs, outputs, &reconfig, reply)) {
      wifiControl(false);
      return pause(false, pulsed, &lag);
    }
  }

  // so we only need to call the act method in if there are no inputs.
  if (Config.inputs[0] == '\0' && Config.outputs[0] != '\0') {
    setPins(Config.outputs, outputs);
    if (!request(RequestAct, NULL, outputs, &reconfig, reply)) {
      wifiControl(false);
      return pause(false, pulsed, &lag);
    }
  }

  if (reconfig && !config()) {
    wifiControl(false);
    return pause(false, pulsed, &lag);
  }

  if (*varsum != VarSum) {
    if (!getVars(vars, &changed)) {
      wifiControl(false);
      return pause(false, pulsed, &lag);
    }
    if (changed) {
      if (Debug) Serial.println(F("Persistent variable(s) changed"));
      writeVars(vars);
    }
    *varsum = VarSum;
  }

  wifiControl(false);

  // Adjust for pulse timing inaccuracy and network time.
  pause(true, pulsed, &lag);
  cyclePin(LED_PIN, 1, false);
  if (Config.monPeriod == Config.actPeriod) {
    if (Debug) Serial.println(F("Cycle complete"));
    return true;
  }
  
  long remaining;
  if (Config.actPeriod * 1000L > pulsed) {
    remaining = (Config.monPeriod - Config.actPeriod) * 1000L;
  } else {
    remaining = Config.monPeriod * 1000L - pulsed;
  }
  if (remaining > lag) {
    remaining -= lag;
    if (Debug) Serial.print(F("Deep sleeping for ")), Serial.print(remaining), Serial.println(F("ms"));
    ESP.deepSleep(remaining * 1000L);
  }
  return true;
}

// calculated elapsed milliseconds taking into account rollover.
// Note that an unsigned long is only 32 bits.
unsigned long elapsedMillis(unsigned long from) {
  unsigned long elapsed, now = millis();
  if (now >= from) {
    elapsed = now - from;
  } else {
    elapsed = (0xffffffff - from) + now; // Rolled over.
  }
  return elapsed;
}

} // end namespace
