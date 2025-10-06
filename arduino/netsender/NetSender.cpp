/*
  Name:
    NetSender - an Arduino library for sending measured values to the cloud and writing values from the cloud.
      
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

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <cstdarg>
#include <cstdio>
#include "config.h"

#if defined ESP8266 || defined ESP32
#include <EEPROM.h>
#include <Preferences.h>
#endif
#ifdef __linux__
#include "nonarduino.h"
#endif

#include "NetSender.h"

namespace NetSender {

// Hardware constants.
#ifdef ESP8266
#define ALARM_PIN              0    // GPIO pin corresponding to the alarm LED (red).
#define ALARM_LEVEL            LOW  // Level indicating an alarm state.
#define NAV_PIN                2    // GPIO pin corresponding to nav light (yellow).
#define STATUS_PIN             2    // GPIO pin corresponding to status LED (blue).
#define BAT_PIN                0    // Analog pin that measures battery voltage.
#define DUTY_CYCLE             150  // Duty cycle used when flashing STATUS_PIN.
#endif
#if defined ESP32 || defined __linux__
#define ALARM_PIN              5    // GPIO pin corresponding to the alarm LED (red).
#define ALARM_LEVEL            HIGH // Level indicating an alarm state.
#define NAV_PIN                19   // GPIO pin corresponding to nav light (yellow).
#define STATUS_PIN             23   // GPIO pin corresponding to status LED (blue).
#define BAT_PIN                4    // Analog pin that measures battery voltage.
#define DUTY_CYCLE             50   // Duty cycle used when flashing STATUS_PIN.
#endif
#define NUM_RELAYS             4    // Number of relays.

// Default values.
#define PEAK_VOLTAGE           845  // Default peak voltage, approximately 25.6V.
#define AUTO_RESTART           600  // Elapsed seconds (10 mins) for automatic restart after an alarm.

#define RETRY_PERIOD           5    // Seconds between retrying after a failure.
#define HEARTBEAT_ATTEMPTS     5    // Number of times we'll attempt to send a heartbeat.

// Preferences.
namespace pref {
  constexpr const char* NameSpace = "NetSender";
  constexpr const char* Mode      = "mode";
}

// Status codes define how many times the status LED is flashed for the given condition.
enum statusCode {
  statusOK           = 1,
  statusConfigError  = 2,
  statusWiFiError    = 3,
  statusConfigUpdate = 4,
  statusVoltageAlarm = 5,
  statusRestart      = 6,
};

// Persistent var names. Keep in sync with pvIndex.
const char* PvNames[] = {
  "LogLevel",
  "Pulses",
  "PulseWidth",
  "PulseDutyCycle",
  "PulseCycle",
  "AutoRestart",
  "AlarmPeriod",
  "AlarmNetwork",
  "AlarmVoltage",
  "AlarmRecoveryVoltage",
  "PeakVoltage",
  "HeartbeatPeriod"
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
  xBat,
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
  bool on;          // Default logic level.
} PowerPin;

// Power pins.
// NB: Update this array if the controller board is revised.
// Power0 controls network equipment and should be on by default.
#ifdef ESP8266
PowerPin PowerPins[] = {
  {0,  "Power0", true},
  {16, "Power1", false},
  {14, "Power2", false},
  {15, "Power3", false},
};
#endif
#if defined ESP32 || defined __linux__
PowerPin PowerPins[] = {
  {18, "Power0", true},
  {32, "Power1", false},
  {33, "Power2", false},
  {25, "Power3", false},
};
#endif

// Variable types, which include both persistent vars and vars associated with power pins. PulseSuppress is included for convenience.
const char* VarTypes = "{\"LogLevel\":\"uint\", \"Pulses\":\"uint\", \"PulseWidth\":\"uint\", \"PulseDutyCycle\":\"uint\", \"PulseCycle\":\"uint\", \"AutoRestart\":\"uint\", \"AlarmPeriod\":\"uint\", \"AlarmNetwork\":\"uint\", \"AlarmVoltage\":\"uint\", \"AlarmRecoveryVoltage\":\"uint\", \"PeakVoltage\":\"uint\", \"HeartbeatPeriod\":\"uint\", \"Power0\":\"bool\", \"Power1\":\"bool\", \"Power2\":\"bool\", \"Power3\":\"bool\", \"PulseSuppress\":\"bool\"}";

const char* logLevels[] = {"", "Error", "Warning", "Info", "Debug"};

// Exported globals.
bool Configured = false;
char MacAddress[MAC_SIZE];
Configuration Config;
ReaderFunc ExternalReader = NULL;
ReaderFunc PostReader = NULL;
int VarSum = 0;
HandlerManager Handlers;
unsigned long RefTimestamp = 0;
BaseHandler *Handler;
String Error = error::None;
Preferences Prefs;

// Other globals.
static int XPin[xMax] = {100000, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0};
static unsigned long Time = 0;
static unsigned long AlarmedTime = 0;
static unsigned long HeartbeatTime = 0;
static int SimulatedBat = 0;

// Utilities:

// isOffline returns true in offline mode
bool isOffline() {
  return (Handler != NULL && strcmp(Handler->name(), mode::Offline) == 0);
}

// log prints a message if the given level is less than or equal to the LogLevel var level,
// or if the system is not yet configured.
void log(LogLevel level, const char* format, ...) {
  if (Configured && (level > Config.vars[pvLogLevel] || level < logNone)) {
    return;
  }

  printf("%s: ", logLevels[level]);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
}

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

// fmtLevel formats a logic level as a string.
const char * fmtLevel(int level) {
  switch (level) {
  case LOW:
    return "LOW";
  case HIGH:
    return "HIGH";
  default:
    return "";
  }
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
// isValidPinName returns true if the given name is the letter A, D,
// or X followed by one or two digits, or false otherwise. If len is
// zero, the name must be null-terminated.
bool isValidPinName(const char *name, size_t len) {
  if (len == 0) {
    len = strlen(name);
  }
  if (len > PIN_SIZE-1) {
    return false;
  }
  switch(name[0]) {
  case 'A':
  case 'D':
  case 'X':
    if (!isdigit(name[1])) {
      return false;
    }
    if (len == 2) {
      return true; // Single-digit pin.
    }
    if (!isdigit(name[2])) {
      return false;
    }
    return true; // Double-digit pin.
  }
  return false;
}

// checkPins returns the number of valid comma-separated pin names, or
// -1 if any pin is invalid or the number of pins exceeds MAX_PINS.
int checkPins(const char * names) {
  const char *start = names;
  int ii = 0;
  while (*start != '\0') {
    const char * finish = strchr(start, ',');
    if (finish == NULL) {
      if (!isValidPinName(start, 0)) {
        return -1;
      }
      ii++;
      break;
    }
    if (!isValidPinName(start, finish-start)) {
      return -1;
    }
    ii++;
    start = finish + 1;
  }
  if (ii > MAX_PINS) {
    return -1;
  }
  return ii;
}

// setPins sets pin names in the Pin array from comma-separated names, clears unused pins and returns the size in use.
// NB: Silently ignores invalid pins, which should have been checked previously.
int setPins(const char * names, Pin * pins) {
  const char *start = names;
  int ii = 0;
  while (*start != '\0' && ii < MAX_PINS) {
    const char * finish = strchr(start, ',');
    if (finish == NULL) {
      if (isValidPinName(start, 0)) {
        strcpy(pins[ii].name, start);
        ii++;
      }
      break;
    }
    size_t len = finish - start;
    if (isValidPinName(start, len)) {
      strncpy(pins[ii].name, start, len);
      pins[ii].name[len] = '\0';
      ii++;
    }
    start = finish + 1;
  }
  int sz = ii;
  for (; ii < MAX_PINS; ii++) {
    pins[ii].name[0] = '\0';
  }
  return sz;
}

// resetPowerPins resets all power pins and writes the ESP32 alarm pin.
// When alarm is true, all pins are set to LOW regardless of their default level.
// For the ESP32, the separate alarm pin is also set.
void resetPowerPins(bool alarm) {
  int level;
  for (int ii = 0; ii < NUM_RELAYS; ii++) {
    level = PowerPins[ii].on ? HIGH : LOW;
    if (alarm) {
      level = LOW;
    }
    pinMode(PowerPins[ii].pin, OUTPUT);
    digitalWrite(PowerPins[ii].pin, level);
    log(logDebug, "Set power pin: D%d %s", PowerPins[ii].pin, fmtLevel(level));
  }
#ifdef ESP32
  level = alarm ? ALARM_LEVEL : !ALARM_LEVEL;
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, level);
  log(logDebug, "Set alarm pin: D%d %s", ALARM_PIN, fmtLevel(level));
#endif
}

// initPins initializes digital pins. On startup, power pins are also initialized.
void initPins(bool startup) {
  log(logDebug, "Initializing pins");
  Pin pins[MAX_PINS];

  for (int ii = 0, sz = setPins(Config.inputs, pins); ii < sz; ii++) {
    if (pins[ii].name[0] == 'D' || pins[ii].name[0] == 'A') {
      int pn = atoi(pins[ii].name + 1);
      pinMode(pn, INPUT);
      log(logDebug, "Set %s as INPUT", pins[ii].name);
    }
  }

  for (int ii = 0, sz = setPins(Config.outputs, pins); ii < sz; ii++) {
    if (pins[ii].name[0] == 'D') {
      int pn = atoi(pins[ii].name + 1);
      log(logDebug, "Set %s as OUTPUT", pins[ii].name);
      pinMode(pn, OUTPUT);
    }
  }

  if (startup) {
    resetPowerPins(false);
  }
}

// readPin reads a pin value and returns it, or -1 upon error.
// The data field will be set in the case of POST data, otherwise it will be NULL.
// When SimulatedBat is non-zero, this value is returned as the value for BAT_PIN one time only.
// The following call to read BAT_PIN will therefore always return the actual value.
int readPin(Pin * pin) {
  int pn = atoi(pin->name + 1);
  pin->value = -1;
  pin->data = NULL;
  switch (pin->name[0]) {
  case 'A':
    if (pn == BAT_PIN && SimulatedBat != 0) {
      log(logDebug, "Simulating battery voltage");
      pin->value = SimulatedBat;
      SimulatedBat = 0;
    } else {
      if (Time < Config.monPeriod * 1000L) {
        // Take a few dummy measurements when starting to allow ADC circuitry to settle.
        analogRead(pn);
	delay(20);
        analogRead(pn);
	delay(20);
	analogRead(pn);
	delay(20);
      }
      pin->value = analogRead(pn);
    }
    break;
  case 'T':
  case 'B':
    if (PostReader != NULL) {
      pin->value = (*PostReader)(pin);
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
  default:
    log(logWarning, "Invalid read from pin %s", pin->name);
    return -1;
  }
  log(logDebug, "Read %s=%d", pin->name, pin->value);
  return pin->value;
}

// setAlarmTimer sets/resets the alarm timer.
void setAlarmTimer(int level) {
  if (level == ALARM_LEVEL) {
    if (AlarmedTime == 0) {
      AlarmedTime = millis();
      log(logDebug, "Alarm timer ON");
    } else {
      log(logDebug, "Alarm timer continuing");
    }
  } else {
    log(logDebug, "Alarm timer OFF");
    AlarmedTime = 0;
  }
}

// writePin writes a pin, with writes to the alarm pin stopping/starting the alarm timer.
void writePin(Pin * pin) {
  int pn = atoi(pin->name + 1);
  PowerPin * pp;
  log(logDebug, "Write %s=%d", pin->name, pin->value);
  switch (pin->name[0]) {
  case 'A':
    analogWrite(pn, pin->value);
    break;
  case 'D':
    if (pn == ALARM_PIN) {
      // Set/reset the alarm timer when writing the alarm pin.
      setAlarmTimer(pin->value);
    }
    digitalWrite(pn, pin->value);
    break;
  case 'X':
    switch (pn) {
    case xBat:
      SimulatedBat = pin->value;
      log(logDebug, "Set simulated battery voltage: %d", pin->value);
      break;
    case xPulseSuppress:
      if (pin->value == 1) {
        XPin[xPulseSuppress] = 1;
      }
      break;
    }
    break;
  default:
    log(logWarning, "Invalid write to pin %s", pin->name);
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
  if (XPin[xPulseSuppress]) {
    log(logDebug, "Pulse suppressed: %ds", pulses * width);
  } else {
    log(logDebug, "Pulsing %d,%d,%d", pulses, width, dutyCycle);
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

// cycles a digital pin on and off, unless we're offline of we're an
// ESP8266 is in pulse mode, returning the number of milliseconds.
int cyclePin(int pin, int cycles) {
  if (isOffline()) {
    return 0;
  }
#ifdef ESP8266
  if (Config.vars[pvPulses] != 0) return 0;
#endif
  pulsePin(pin, cycles, 1, DUTY_CYCLE);
  return cycles*1000;
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
  // Only clear the config if there's been a minor version change.
  if (config->version/10 != VERSION/10) {
    log(logDebug, "Clearing config with version %d", config->version);
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
  Serial.print(F("Configuration size: ")), Serial.println(sizeof(Configuration));
  Serial.print(F("Configration version: ")), Serial.println(Config.version);
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
  log(logDebug, "Writing config");
  EEPROM.begin(sizeof(Configuration));
  for (int ii = 0; ii < sizeof(Configuration); ii++) {
    EEPROM.write(ii, *bytep++);
  }
  EEPROM.commit();
  log(logDebug, "Wrote config");
  printConfig();
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
    if (!XPin[xAlarmed]) {
      return; // Nothing to do
    }
    log(logDebug, "Cleared alarm");
    resetPowerPins(false);
    XPin[xAlarmed] = false;
    AlarmedTime = 0;
    return;
  }
  if (Config.vars[pvAlarmNetwork] == 0 && Config.vars[pvAlarmVoltage] == 0) {
    return;
  }
  log(logDebug, "Set alarm");
  resetPowerPins(true);
  XPin[xAlarms]++;

  if (continuous) {
    XPin[xAlarmed] = true;
    if (AlarmedTime == 0) {
      AlarmedTime = millis();
    }
    return;
  }

  // Alarm is temporary.
  log(logDebug, "Alarming for %ds", Config.vars[pvAlarmPeriod]);
  delay(Config.vars[pvAlarmPeriod] * 1000);
  log(logDebug, "Cleared temporary alarm");
  resetPowerPins(false);
  XPin[xAlarmed] = false;
}

// restart restarts the ESP8266, saving the reason, and raising an
// alarm before restarting when alarm is true.
void restart(bootReason reason, bool alarm) {
  log(logInfo, "**** Restarting (%d,%d) ****", reason, alarm);

  if (reason != Config.boot) {
    log(logDebug, "Writing boot reason: %d", reason);
    Config.boot = reason;
    writeConfig(&Config);
  }
  resetPowerPins(false);
  if (alarm) {
    writeAlarm(true, true);
    delay(2000);
  }
  cyclePin(STATUS_PIN, statusRestart);
  ESP.restart();
}

// request config, and return true upon success, or false otherwise
// Side effects:
//   Sets Configured global to true upon success.
// ToDo: Iterate if the request returns a reconfig request.
bool config() {
  String reply, error, param;
  bool reconfig;
  bool changed = false;
  Pin pins[2];

  log(logDebug, "Getting config");
  // As of v160, var types (vt) are sent with config requests.
  strcpy(pins[0].name, "vt");
  pins[0].value = strlen(VarTypes);
  pins[0].data = (byte*)VarTypes;
  pins[1].name[0] = '\0';

  if (!Handler->request(RequestConfig, pins, NULL, &reconfig, reply) || extractJson(reply, "er", param)) {
    cyclePin(STATUS_PIN, statusConfigError);
    return false;
  } 
  log(logDebug, "Config response: %s", reply.c_str());

  if (extractJson(reply, "mp", param) && param.toInt() != Config.monPeriod) {
    Config.monPeriod = param.toInt();
    log(logDebug, "Mon. period changed: %d", Config.monPeriod);
    changed = true;
  }
  if (extractJson(reply, "ap", param) && param.toInt() != Config.actPeriod) {
    Config.actPeriod = param.toInt();
    log(logDebug, "Act. period changed: %d", Config.actPeriod);
    changed = true;
  }
  if (extractJson(reply, "wi", param) && param != Config.wifi) {
    padCopy(Config.wifi, param.c_str(), WIFI_SIZE);
    log(logDebug, "Wifi changed: %s", Config.wifi);
    changed = true;
  }
  if (extractJson(reply, "dk", param) && param != Config.dkey) {
    padCopy(Config.dkey, param.c_str(), DKEY_SIZE);
    log(logDebug, "Dkey changed: %s", Config.dkey);
    changed = true;
  }
  if (extractJson(reply, "ip", param) && param != Config.inputs) {
    if (checkPins(param.c_str()) >= 0) {
      padCopy(Config.inputs, param.c_str(), IO_SIZE);
      log(logDebug, "Inputs changed: %s", Config.inputs);
      changed = true;
    } else {
      log(logWarning, "Invalid inputs: %s", param.c_str());
    }
  }
  if (extractJson(reply, "op", param) && param != Config.outputs) {
    if (checkPins(param.c_str()) >= 0) {
      padCopy(Config.outputs, param.c_str(), IO_SIZE);
      log(logDebug, "Outputs changed: %s", Config.outputs);
      changed = true;
    } else {
      log(logWarning, "Invalid outputs: %s", param.c_str());
    }
  }

  if (changed) {
    writeConfig(&Config);
    initPins(false); // NB: Don't re-initalize power pins.
    cyclePin(STATUS_PIN, statusConfigUpdate);
  }
  Configured = true;
  return true;
}

// Retrieve vars from cloud, return the persistent vars and
// set changed to true if any persistent var has changed.
// Transient vars, such as "id" or "error" are not saved.
// Missing persistent vars default to 0, except for peak voltage and auto restart.
// Side effects:
//   - RefTimestamp is set to supplied timestamp (ts), unless already set.
bool getVars(int vars[MAX_VARS], bool* changed, bool* reconfig) {
  String reply, error, id, mode, param, var;
  *changed = false;

  log(logDebug, "Getting vars");
  if (!Handler->request(RequestVars, NULL, NULL, reconfig, reply) || extractJson(reply, "er", param)) {
    return false;
  }
  auto hasId = extractJson(reply, "id", id);
  if (hasId) log(logDebug, "id=%s", id.c_str());

  var = hasId ? id + ".error" : "error";
  auto hasError = extractJson(reply, var.c_str(), error);
  if (hasError) {
    log(logDebug, "error=%s", error.c_str());
     Error = error; // NB: We allow the error to be overwritten for testing only.
  }

  var = hasId ? id + ".mode" : "mode";
  auto hasMode = extractJson(reply, var.c_str(), mode);
  if (hasMode) {
    auto h = Handlers.get(mode.c_str());
    if (h == NULL) {
      log(logWarning, "Invalid mode %s", mode.c_str());
    } else if (mode != Handler->name()) {
      // Save mode to ESP's non-volatile storage (read-write).
      if (Prefs.begin(pref::NameSpace, false)) {
        Prefs.putString(pref::Mode, mode);
        Prefs.end();
      } else {
        log(logError, "Failed to open Preferences namespace %s", pref::NameSpace);
      }
      log(logDebug, "updated mode=%s", mode.c_str());
      Handler = h;
      Error = error::None; // Clear error, if any.
    } // else mode unchanged
  }

  auto hasRc = extractJson(reply, "rc", param);
  if (hasRc) {
    log(logDebug, "rc=%s", param.c_str());
    auto rc = param.toInt();
    if (rc == rcUpdate) {
      *reconfig = true;
    }
  }

  auto hasTs = extractJson(reply, "ts", param);
  if (hasTs) {
    log(logDebug, "ts=%s", param.c_str());
    if (RefTimestamp == 0) {
      RefTimestamp = strtoul(param.c_str(), NULL, 10);
      log(logInfo, "RefTimestamp=%lu", RefTimestamp);
    }
  }

  for  (int ii = 0; ii < MAX_VARS; ii++) {
    int val = 0;
    if (hasId) {
      var = id + '.' + String(PvNames[ii]);
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

    log(logDebug, "%s=%d", PvNames[ii], vars[ii]);
    if (Config.vars[ii] != val) {
      *changed = true;
      log(logDebug, "%s=>%d", PvNames[ii], val);
     }
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
  log(logDebug, "Writing vars");
  memcpy(Config.vars, vars, sizeof(Config.vars));
  writeConfig(&Config);
}

// init should be called from setup once
void init(void) {
  Serial.begin(115200);
  pinMode(ALARM_PIN, OUTPUT);
  pinMode(NAV_PIN, OUTPUT);
  pinMode(STATUS_PIN, OUTPUT);
#ifdef ESP8266
  digitalWrite(STATUS_PIN, HIGH);
#endif

  // Get Config.
  readConfig(&Config);

  // Get boot info.
  XPin[xBoot] = Config.boot;
  Serial.print(F("Boot reason: ")), Serial.println(Config.boot);

  // Initialize GPIO pins, including power pins.
  initPins(true);

  // Add handlers and set active handler.
  Handlers.add(new OnlineHandler);
  #ifdef FEATURE_OFFLINE
  Handlers.add(new OfflineHandler);
  #endif

  // Get mode from ESP's non-volatile storage (read-only), or default to online mode.
  if (Prefs.begin(pref::NameSpace, true)) {
    Handler = Handlers.set(Prefs.getString(pref::Mode).c_str());
    Prefs.end();
  } else {
    log(logWarning, "Failed to open Preferences namespace %s", pref::NameSpace);
  }

  if (Handler == NULL) {
    log(logDebug, "Defaulting to online mode");
    Handler = Handlers.set(mode::Online);
  }
}

// Pause to maintain timing accuracy, adjusting the timing lag in the process.
// Pulsed is how long we've pulsed in milliseconds, or the equivalent delay if suppressing pulses.
// If we're here because of a problem and we're not pulsing, we just pause long enough to retry,
// since timing accuracy is moot. If pulsing, we pause for the active time remaining this cycle,
// unless we're out of time.
bool pause(bool ok, unsigned long pulsed, long * lag) {
  Handler->disconnect();

  if (!ok && pulsed == 0) {
    log(logInfo, "Retrying in %ds", RETRY_PERIOD);
    delay(RETRY_PERIOD * 1000L);
    return ok;
  }

  unsigned long now = millis();
  long remaining = Config.actPeriod * 1000L - pulsed;
  *lag += (now - Time - pulsed);

  log(logDebug, "Pulsed time: %ums", pulsed);
  log(logDebug, "Total lag: %ldms", *lag);
  log(logDebug, "Run time: %ulms", now - Time);

  if (remaining > *lag) {
    remaining -= *lag;
    log(logDebug, "Pausing for %ldms", remaining);
    delay(remaining);
    *lag = 0;
  } else {
    log(logDebug, "Skipped pause");
  }
  return ok;
}

// setError notifies the service of an error and updates the Error global upon success.
// ToDo: validate the error.
bool setError(const char* error) {
  if (Error == error) {
    return true; // Nothing to do.
  }

  auto h = Handlers.get(mode::Online);
  if (h == NULL) {
    log(logError, "Could not get online handler to send error");
    return false;
  }

  auto err = Error;
  Error = error;
  bool reconfig;
  String reply;
  auto ok = h->request(RequestConfig, NULL, NULL, &reconfig, reply);
  h->disconnect();

  if (ok) {
    log(logDebug, "error=%s", error);
    return true;
  }
  Error = err;
  log(logWarning, "Failed to notify service of error, error unchanged");
  return false;
}

// run should be called from loop until it returns true, e.g., 
//  while (!run(&varsum)) {
//    ;
//  }
// Connecting to WiFi is handled by the request handler (if required),
// but we call disconnect here to ensure WiFi is not left on.
// If a config request fails, we pause then re-try.
// If other requests fails, we simply log and continue.
// NB: pulse suppression must be re-enabled each cycle via the X14 pin.
bool run(int* varsum) {
  log(logDebug, "---- starting run cycle ----");
  Pin inputs[MAX_PINS], outputs[MAX_PINS];
  String reply;
  bool reconfig = false;
  unsigned long pulsed = 0;
  long lag = 0;
  unsigned long now = millis();
  int vars[MAX_VARS];
  bool changed;
  bool heartbeat = (Time == 0); // Always check in upon restart.

  log(logDebug, "Configured: %s", Configured ? "true" : "false");

  // Measure lag to maintain accuracy between cycles.
  if (Time > 0) {
    if (now < Time) {
      log(logDebug, "Rolled over");
      lag = (long)(UINT_MAX - Time + now) - (Config.monPeriod * 1000L);
      RefTimestamp += (UINT_MAX/1000);
    } else {
      lag = (long)(now - Time) - (Config.monPeriod * 1000L);
    }
    log(logDebug, "Initial lag: %ldms", lag);
    if (lag < 0) {
      lag = 0;
    }
  }
  Time = now; // record the start of each cycle

  // Check if it's time to do a heartbeat.
  if (isOffline() && Config.vars[pvHeartbeatPeriod] > 0 && (now-HeartbeatTime)/1000 >= Config.vars[pvHeartbeatPeriod]) {
    log(logInfo, "Issuing heartbeat.");
    heartbeat = true;
  }

  if (heartbeat) {
    auto ok = false;
    auto nw = Config.vars[pvAlarmNetwork];
    Config.vars[pvAlarmNetwork] = 0; // Suppress network alarm.
    for (int attempts = 0; attempts < HEARTBEAT_ATTEMPTS; attempts++) {
      ok = getVars(vars, &changed, &reconfig);
      if (ok) {
        break;
      }
      pause(false, 0, &lag);
    }
    Config.vars[pvAlarmNetwork] = nw; // Restore network alarm, if any.

    if (ok) {
      if (changed) {
        log(logDebug, "Persistent vars changed after restart/heartbeat.");
        writeVars(vars);
      }

      if (reconfig && config()) {
        reconfig = false;
      } // Else try later.

      *varsum = VarSum;

    } else {
      log(logWarning, "Failed to get vars after restart/heartbeat.");
    }

    // Always turn off Wi-Fi afterward to ensure stable pin reads and save power.
    Handler->disconnect();
    HeartbeatTime = now;
  }

  // Restart if the alarm has gone on for too long.
  // NB: Check AlarmedTime regardless of whether XPin[xAlarmed] is true or not.
  if (AlarmedTime > 0) {
    int alarmed; // Alarmed duration in seconds.
    if (now >= AlarmedTime) {
      alarmed = (now - AlarmedTime)/1000;
    } else { // rolled over
      alarmed = ((UINT_MAX - AlarmedTime) + now)/1000;
    }
    log(logDebug, "Alarm duration: %ds", alarmed);
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
    pulsePin(NAV_PIN, Config.vars[pvPulses], Config.vars[pvPulseWidth], Config.vars[pvPulseDutyCycle]);
    pulsed = (unsigned long)Config.vars[pvPulses] * Config.vars[pvPulseWidth] * 1000L;
    long gap = (Config.vars[pvPulseCycle] * 1000L) - (long)pulsed;
    if (gap > 0) {
      for (int spanned = 0; spanned < Config.monPeriod - Config.vars[pvPulseCycle]; spanned += Config.vars[pvPulseCycle]) {
        log(logDebug, "Pulse group gap: %dms", gap);
        delay(gap);
        pulsePin(NAV_PIN, Config.vars[pvPulses], Config.vars[pvPulseWidth], Config.vars[pvPulseDutyCycle]);
        pulsed += (gap + ((unsigned long)Config.vars[pvPulses] * Config.vars[pvPulseWidth]));
      }
    }
  }
  XPin[xPulseSuppress] = 0;

  // Check battery voltage if we have an alarm voltage.
  if (Config.vars[pvAlarmVoltage] > 0) {
    Pin pin = { .name = {'A', (char)('0'+BAT_PIN), '\0'} };
    log(logDebug, "Checking battery voltage");
    XPin[xBat] = readPin(&pin);
    if (XPin[xBat] < Config.vars[pvAlarmVoltage]) {
      log(logWarning, "Battery is below alarm voltage!");
      setError(error::LowVoltage);
      log(logDebug, "Checking Alarmed pin");
      if (!XPin[xAlarmed]) {
        log(logWarning, "Alarmed pin is not currently alarmed, writing alarm pin");
        // low voltage; raise the alarm.
        cyclePin(STATUS_PIN, statusVoltageAlarm);
        writeAlarm(true, true);
      } else {
        log(logDebug, "Alarmed pin is currently alarmed, no action required");
      }
      return pause(false, pulsed, &lag); // Turns off WiFi.
    }
    log(logDebug, "Checking Alarmed pin");
    if (XPin[xAlarmed]) {
      log(logDebug, "Currently alarmed, checking voltage against recovery voltage");
      if (XPin[xBat] < Config.vars[pvAlarmRecoveryVoltage]) {
        return pause(false, pulsed, &lag);
      }
      log(logInfo, "Low voltage alarm cleared");
      setError(error::None);
      writeAlarm(false, true);
    } else {
      log(logDebug, "Alarmed pin is not currently alarmed");
      if (Error == error::LowVoltage) {
        log(logDebug, "Error is currently LowVoltage but it shouldn't be; changing to None");
        setError(error::None);
      }
    }
    if (XPin[xBat] > Config.vars[pvPeakVoltage]) {
      log(logWarning, "High voltage, pin value: %d, peak voltage: %d", XPin[xBat], Config.vars[pvPeakVoltage]);
    }
  } else {
    XPin[xBat] = -1;
    log(logDebug, "Skipped voltage check");
  }

  Handler->disconnect(); // Disconnect WiFi before taking measurements.

  // Read inputs, if any.
  // NB: We do this before we are connected to the network.
  log(logDebug, "Reading pins");
  for (int ii = 0, sz = setPins(Config.inputs, inputs); ii < sz; ii++) {
    readPin(&inputs[ii]);
  }

  // Attempt configuration whenever:
  //   (1) there are no inputs and no outputs, or
  //   (2) we received a reconfig request earlier
  if (reconfig || (Config.inputs[0] == '\0' && Config.outputs[0] == '\0')) {
    if (!config()) {
      log(logDebug, "Config request failed (%s)", Error);
      return pause(false, pulsed, &lag);
    }
    reconfig = false;
  }

  // Since version 138 the poll method returns outputs as well as inputs.
  if (Config.inputs[0] != '\0') {
    setPins(Config.outputs, outputs);
    if (!Handler->request(RequestPoll, inputs, outputs, &reconfig, reply)) {
      log(logDebug, "Poll request failed (%s)", Error);
    }
  }

  // so we only need to call the act method in if there are no inputs.
  if (Config.inputs[0] == '\0' && Config.outputs[0] != '\0') {
    setPins(Config.outputs, outputs);
    if (!Handler->request(RequestAct, NULL, outputs, &reconfig, reply)) {
      log(logDebug, "Act request failed (%s)", Error);
    }
  }

  if (reconfig && !config()) {
    log(logDebug, "Config request failed (%s)", Error);
    return pause(false, pulsed, &lag);
  }

  if (*varsum != VarSum) {
    if (!getVars(vars, &changed, &reconfig)) {
      log(logDebug, "Vars request failed (%s)", Error);
      return pause(false, pulsed, &lag);
    }
    if (changed) {
      log(logDebug, "Persistent variable(s) changed");
      writeVars(vars);
    }
    *varsum = VarSum;
  }

  // Indicate completion of the cycle and adjust pulsed time.
  // ToDo: This is a debug feature that could be removed.
  pulsed += cyclePin(STATUS_PIN, statusOK);
  // Adjust for pulse timing inaccuracy and network time.
  pause(true, pulsed, &lag);
  if (Config.monPeriod == Config.actPeriod) {
    log(logDebug, "Cycle complete");
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
    log(logDebug, "Deep sleeping for %dms", remaining);
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
    elapsed = (UINT_MAX - from) + now; // Rolled over.
  }
  return elapsed;
}

} // end namespace
