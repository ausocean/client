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

namespace NetSender {

#ifdef ESP8266
#define VERSION                179
#define MAX_PINS               10
#define DKEY_SIZE              20
#define RESERVED_SIZE          48
#endif
#if defined ESP32 || defined __linux__
#define VERSION                214
#define MAX_PINS               20
#define DKEY_SIZE              32
#define RESERVED_SIZE          64
#endif

#define WIFI_SIZE              80
#define PIN_SIZE               4
#define IO_SIZE                (MAX_PINS * PIN_SIZE)
#define MAX_VARS               11

typedef enum {
  RequestConfig = 0,
  RequestPoll   = 1,
  RequestAct    = 2,
  RequestVars   = 3,
} RequestType;

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

// exported globals
extern Configuration Config;
extern ReaderFunc ExternalReader;
extern ReaderFunc BinaryReader;
extern int VarSum;

// init should be called from setup once.
// run should be called from loop until it returns true, e.g., 
//  while (!run(&vs)) {
//    delay(RETRY_PERIOD * (long)1000);
//  }
extern void init();
extern bool run(int*);

} // end namespace
#endif
