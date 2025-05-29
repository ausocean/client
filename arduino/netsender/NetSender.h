/*
  Name:
    NetSender - an Arduino library for sending measured values to the cloud and writing values from the cloud.

  Description:
    See https://www.cloudblue.org

  License:
    Copyright (C) 2017-2025 The Australian Ocean Lab (AusOcean).

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

#ifndef NetSender_H
#define NetSender_H

#include "Arduino.h"

namespace NetSender {

#ifdef ESP8266
#define VERSION                181
#define MAX_PINS               10
#define DKEY_SIZE              20
#define RESERVED_SIZE          48
#endif
#if defined ESP32 || defined __linux__
#define VERSION                10016
#define MAX_PINS               20
#define DKEY_SIZE              32
#define RESERVED_SIZE          64
#endif

#define MAC_SIZE               18
#define WIFI_SIZE              80
#define PIN_SIZE               4
#define IO_SIZE                (MAX_PINS * PIN_SIZE)
#define MAX_VARS               11
#define MAX_HANDLERS           2

// Device modes.
namespace mode {
  constexpr const char* Online = "Normal";
  constexpr const char* Offline = "Offline";
}

// Device requests.
typedef enum {
  RequestConfig = 0,
  RequestPoll   = 1,
  RequestAct    = 2,
  RequestVars   = 3,
} RequestType;

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

// Boot codes.
enum bootReason {
  bootNormal = 0x00, // Normal reboot (operator requested).
  bootWiFi   = 0x01, // Reboot due to error when trying to disconnect from Wifi.
  bootAlarm  = 0x02, // Alarm auto-restart.
};

// Log levels for use with log function.
typedef enum logLevel {
  logNone    = 0,
  logError   = 1,
  logWarning = 2,
  logInfo    = 3,
  logDebug   = 4,
  logMax     = 5
} LogLevel;

// Persistent variables (stored in EEPROM as part of configuration).
// NB: Keep indexes in sync with names, and update MAX_VARS if needed.
enum pvIndex {
  pvLogLevel,
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

// Configuration parameters are saved to the first 256 or 384 bytes of EEPROM
// for the ESP8266 or ESP32 respectively as follows:
//   Version        (length 2)
//   Mon. period    (length 2)
//   Act. period    (length 2)
//   Boot           (length 2)
//   WiFi ssid,key  (length 80)
//   Device key     (length 20 or 32)
//   Inputs         (length 40 or 80) // 10 or 20 x 4
//   Outputs        (length 40 or 80) // 10 or 20 x 4
//   Vars           (length 20 or 40) // 10 x 2 or 4
//   Reserved       (length 48 or 64)
typedef struct {
  short version;
  short monPeriod;
  short actPeriod;
  short boot;
  char wifi[WIFI_SIZE];
  char dkey[DKEY_SIZE];
  char inputs[IO_SIZE];
  char outputs[IO_SIZE];
  int  vars[MAX_VARS];
  char reserved[RESERVED_SIZE];
} Configuration;

// Pin represents a pin name and value and optional POST data.
typedef struct {
  char name [PIN_SIZE];
  int value;
  byte * data;
} Pin;

// ReaderFunc represents a pin reading function.
typedef int (*ReaderFunc)(Pin *);

// BaseHandler defines our abstract base handler class.
class BaseHandler {
public:
  virtual const char* name() = 0;
  virtual bool init() = 0;
  virtual bool request(RequestType req, Pin* inputs, Pin* outputs, bool* reconfig, String& reply) = 0;
  virtual bool connect() = 0;
  virtual void disconnect() = 0;
};

// OnlineHandler defines our handler in normal (online) operating mode.
class OnlineHandler : public BaseHandler {
public:
  const char* name() { return mode::Online; }
  bool init() override;
  bool request(RequestType req, Pin* inputs, Pin* outputs, bool* reconfig, String& reply) override;
  bool connect() override;
  void disconnect() override;
private:
  bool connected;
};

// OfflineHandler defines our handler in offline mode.
class OfflineHandler : public BaseHandler {
public:
  const char* name() override { return mode::Offline; };
  bool init() override;
  bool request(RequestType req, Pin* inputs, Pin* outputs, bool* reconfig, String& reply) override;
  bool connect() override { return false; };
  void disconnect() override {};
private:
  bool initialized;
  unsigned long time;
};

// HandlerManager defines our handler manager.
class HandlerManager {
public:
  HandlerManager();
  ~HandlerManager();

  bool add(BaseHandler* handler);     // Add a handler.
  BaseHandler* set(const char* name); // Set the current/active handler and return it.
  BaseHandler* get();                 // Get the current/active handler.
  BaseHandler* get(const char* name); // Get a handler by name.

private:
  BaseHandler* handlers[MAX_HANDLERS];
  size_t size;
  size_t current;
};

// exported globals
extern bool Configured;
extern char MacAddress[MAC_SIZE];
extern Configuration Config;
extern ReaderFunc ExternalReader;
extern ReaderFunc BinaryReader;
extern int VarSum;
extern HandlerManager Handlers;
extern unsigned long RefTimestamp;
extern BaseHandler *Handler;
extern String Error;

// init should be called from setup once.
// run should be called from loop until it returns true, e.g., 
//  while (!run(&vs)) {
//    delay(RETRY_PERIOD * (long)1000);
//  }
extern void init();
extern bool run(int*);
extern void log(LogLevel, const char*, ...);
extern int setPins(const char*, Pin*);
extern void writePin(Pin*);
extern void writeAlarm(bool, bool);
extern bool extractJson(String, const char*, String&);
extern void restart(bootReason, bool);

} // end namespace
#endif
