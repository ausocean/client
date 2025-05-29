/*
  Description:
    NetSender online request handler.
    Writes data to the cloud.

  License:
    Copyright (C) 2025 The Australian Ocean Lab (AusOcean).

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

#include "Arduino.h"
#include "NetSender.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#endif
#ifdef __linux__
#include "nonarduino.h"
#endif

#include "NetSender.h"

namespace NetSender {

// Network constants.
#define SVC_URL       "http://data.cloudblue.org" // Web service.
#define DEFAULT_WIFI  "netreceiver,netsender"     // Default WiFi credentials.

#define MAX_PATH      256   // Maximum URL path size.
#define WIFI_ATTEMPTS 100   // Number of WiFi attempts
#define WIFI_DELAY    100   // Milliseconds between WiFi attempts.
#define HTTP_TIMEOUT  10000 // Millisecond timeout for HTTP connection and request attempts.

// HTTP status codes that we care about.
enum httpStatusCode {
  httpOK                = 200,
  httpMovedPermanently  = 301,
  httpMovedTemporarily  = 302,
  httpSeeOther          = 303,
  httpTemporaryRedirect = 307,
  httpPermanentRedirect = 308,
};

// Our globals.
static IPAddress LocalAddress;
static int NetworkFailures = 0;

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

// wifiOn and wifiOff enable or disable WiFi functionality to conserve power.
// We shouldn't use these functions directly in the run loop, but use wifiBegin() and wifiControl(false) instead.
// NB: Calling WiFi.mode(WIFI_MODE_NULL) produces the following error: "wifi:NAN WiFi stop"
// According to https://github.com/espressif/esp-idf/issues/12473, it can be ignored.
void wifiOn() {
#ifdef ESP8266
  wifi_fpm_do_wakeup();
  wifi_fpm_close();
  wifi_set_opmode(STATION_MODE);
  wifi_station_connect();
#endif
#ifdef ESP32
  WiFi.mode(WIFI_STA);
#endif
  log(logDebug, "WiFi on");
}

void wifiOff() {
#ifdef ESP8266
  wifi_station_disconnect();
  bool stopped = false;
  for (int attempts = 0; !stopped && attempts < WIFI_ATTEMPTS; attempts++) {
    stopped = (wifi_station_get_connect_status() == DHCP_STOPPED);
    delay(WIFI_DELAY);
  }
  if (!stopped) {
    log(logError, "DHCP not stopping.");
    restart(bootWiFi, true);
  }
  wifi_set_opmode(NULL_MODE);
  wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);
#endif
#ifdef ESP32
  WiFi.mode(WIFI_MODE_NULL);
#endif
  delay(WIFI_DELAY);
  log(logDebug, "WiFi off");
}

// wifiControl turns the WiFi on and off, returning true on success,
// false otherwise. A failure to turn on the WiFi is regarded as a
// network failure, although it could be due to the ESP, not the
// network. A failure to turn off the WiFi however is regarded as an
// ESP failure and results in a restart.
bool wifiControl(bool on) {
  if (on) {
    log(logDebug, "Turning WiFi on");
    if (WiFi.status() == WL_CONNECTED) {
      return true; // Nothing to do.
    }
    wifiOn();
    if (!WiFi.mode(WIFI_STA)) {
      log(logError, "WiFi not starting");
      return false;
    }
  } else {
    log(logDebug, "Turning WiFi off");
    WiFi.disconnect();
    for (int attempts = 0; WiFi.status() == WL_CONNECTED && attempts < WIFI_ATTEMPTS; attempts++) {
      delay(WIFI_DELAY);
    }
    if (WiFi.status() == WL_CONNECTED) {
      log(logError, "WiFi not disconnecting");
      restart(bootWiFi, true);
    }
    wifiOff();
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

  log(logDebug, "Requesting DHCP from %s", wifi);
  WiFi.begin(ssid, key);
  delay(WIFI_DELAY);
 
  // NB: connecting can take several seconds, so ensure WIFI_ATTEMPTS x WIFI_DELAY is at least 5000ms.
  for (int attempts = 0; attempts < WIFI_ATTEMPTS; attempts++) {
    if (WiFi.status() == WL_CONNECTED) {
      LocalAddress = WiFi.localIP();
      log(logDebug, "Obtained DHCP IP Address %d.%d.%d.%d", LocalAddress[0], LocalAddress[1], LocalAddress[2], LocalAddress[3]);
      return true;
    }
    delay(WIFI_DELAY);
  }

  log(logDebug, "Failed to connect to WiFi");
  return false;
}

// wifiBegin attempts to begin a WiFi session, first, using the
// configured hotspot and, second, using the default hotspot.
bool wifiBegin() {
  auto ok = wifiControl(true);
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

  auto get = (body.length() == 0);
  log(logDebug, "%s %s", get ? "GET" : "POST", url.c_str());
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
    log(logDebug, "Redirecting to: %s", url.c_str());
    http.end();
    client.stop();
    return httpRequest(url, body, reply); // Redirect to the new location.
  }

  auto ok = (status == httpOK);
  if (ok) {
    reply = http.getString();
    log(logDebug, "Reply: %s", reply.c_str());
  } else {
    log(logWarning, "HTTP request failed with status: %d", status);
  }
  http.end();
  client.stop();
  return ok;
}

// init intializes the online request handler by disabling WiFi
// persistence and recording our MAC address.
bool OnlineHandler::init() {
  log(logDebug, "Initializing online handler");
  
  // Disable WiFi persistence in flash memory.
  WiFi.persistent(false);

  // Connect to WiFi to obtain the MAC address, then disconnect.
  wifiOn();
  delay(2000);
  byte mac[6];
  WiFi.macAddress(mac);
  fmtMacAddress(mac, MacAddress);
  log(logDebug, "Got MAC address: %s", MacAddress);
  wifiOff();

  connected = false;
  return true;
}

// request issue a single request, writing polled values to 'inputs' and actuated values to 'outputs'.
// Config requests (and only config requests) communicate the device mode and error,
// where the mode corresponds to the name of the _active_ request handler.  
// Sets 'reconfig' true if reconfiguration is required, otherwise leaves the value as is.
// Side effects: 
//   Updates VarSum global when differs from the varsum ("vs") parameter.
//   Sets Configured global to false for update and alarm requests.
//   Updates, enters debug mode or alarm mode, or reboots according to the response code ("rc").
bool OnlineHandler::request(RequestType req, Pin * inputs, Pin * outputs, bool * reconfig, String& reply) {
  char path[MAX_PATH];
  String param, body;
  unsigned long ut = millis()/1000;

  switch (req) {
  case RequestConfig:
    sprintf(path, "/config?vn=%d&ma=%s&dk=%s&la=%d.%d.%d.%d&ut=%ld&md=%s&er=%s", VERSION, MacAddress, Config.dkey,
            LocalAddress[0], LocalAddress[1], LocalAddress[2], LocalAddress[3], ut, Handler->name(), Error.c_str());
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
        log(logDebug, "Not sending negative value for %s", inputs[ii].name);
        continue;
      }
      sprintf(path + strlen(path), "&%s=%d", inputs[ii].name, inputs[ii].value);
      // Populate the body with binary data, if any.
      if (inputs[ii].data != NULL && inputs[ii].value > 0) {
        body += String((const char*)(inputs[ii].data));
      }
    }
  }

  if (connect() && httpRequest(String(SVC_URL)+String(path), body, reply)) {
    writeAlarm(false, true);
    NetworkFailures = 0;
  } else {
    NetworkFailures++;
    log(logDebug, "Network failures: %d", NetworkFailures);
    if (Config.vars[pvAlarmNetwork] > 0 && NetworkFailures >= Config.vars[pvAlarmNetwork]) {
      // Too many network failures; raise the alarm!
      writeAlarm(true, false);
      NetworkFailures = 0;
    }
    return false;
  }

  if (!reply.startsWith("{")) {
    log(logWarning, "Malformed response");
    return false;
  }

  // Since version 138 and later, poll requests also return output values.
  if ((outputs != NULL) && (req == RequestPoll || req == RequestAct)) {
    bool found = false;
    for (int ii = 0; ii < MAX_PINS && outputs[ii].name[0] != '\0'; ii++) {
      if (extractJson(reply, outputs[ii].name, param)) {
        outputs[ii].value = param.toInt();
        writePin(&outputs[ii]);
      } else {
        outputs[ii].value = -1;
        log(logWarning, "Missing value for output pin %s", outputs[ii].name);
      }
    }
  }

  if (extractJson(reply, "rc", param)) {
    auto rc = param.toInt();
    log(logDebug, "rc=%d", rc);
    switch (rc) {
    case rcOK:
      break;
    case rcUpdate:
      log(logDebug, "Received update request.");
      *reconfig = true;
      Configured = false;
      break;
    case rcReboot:
      log(logDebug, "Received reboot request.");
      if (Configured) {
        restart(bootNormal, false);
      } // else ignore reboot request unless configured
      break;
    case rcAlarm:
      log(logDebug, "Received alarm request.");
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
    auto vs = param.toInt();
    log(logDebug, "vs=%d", vs);
    if (vs != VarSum) {
      log(logDebug, "Varsum changed");
    }
    VarSum = vs;
  }

  if (extractJson(reply, "er", param)) {
    // we let the caller deal with errors
    log(logDebug, "er=%s", param.c_str());
  }

  return true;
}

// Connect to WiFi, unless already connected.
bool OnlineHandler::connect() {
  if (!connected) {
    connected = wifiBegin();
  }
  return connected;
}

// Disconnect from WiFi, unless already disconnected.
void OnlineHandler::disconnect() {
  if (!connected) {
    return;
  }
  wifiControl(false);
  connected = false;
}

} // end namespace
