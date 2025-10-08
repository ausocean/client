 /*
NAME
  bin-netsender - Example NetSender client for sending binary data.

LICENSE
  Copyright (C) 2019-2025 the Australian Ocean Lab (AusOcean).

  This is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU General Public License
  along with NetSender in gpl.txt.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "NetSender.h"

int varsum;

// 16 bytes of binary test data
byte binData[16] = {'\000', '\001', '\002', '\003', '\004', '\005', '\006', '\007',
                    '\010', '\011', '\012', '\013', '\014', '\015', '\016', '\017' };

// fastReader returns right away.
int fastReader(NetSender::Pin *pin) {
  pin->value = -1;
  if (pin->name[0] != 'B' && pin->name[1] != '0') {
    return -1;
  }

  // send 16 octets of binary data
  pin->value = 16;
  pin->data = binData;
  return pin->value;
}

// slowReader requires 3 passes, each 10-seconds long to complete.
// The pin value and return value are -1 until the reader completes.
int slowReader(NetSender::Pin *pin) {
  static int passes = 0;

  pin->value = -1;
  if (pin->name[0] != 'B' && pin->name[1] != '0') {
    return -1;
  }

  delay(10000); // "work" for 10 seconds
  passes++;
  Serial.print(F("slowReader: pass ")), Serial.println(passes);
  if (passes != 3) {
    return -1; // we're not yet finished
  }

  Serial.println(F("slowReader: complete"));
  passes = 0; // reset
  pin->value = 16;
  pin->data = binData;
  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup(void) {
  NetSender::PostReader = slowReader;
  NetSender::init();
  loop();
}

void loop() {
  while (!NetSender::run(&varsum)) {
    delay(RETRY_PERIOD * (long)1000);
  }
}
