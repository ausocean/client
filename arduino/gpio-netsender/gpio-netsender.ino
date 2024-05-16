/*
NAME
  gpio-netsender - basic NetSender client.

DESCRIPTION
  gpio-netsender is a basic Arduino client for sending analog and
  digital pin values to the cloud and writing digital pin values
  received from the cloud. This client is just a shallow wrapper for
  the NetSender library and implements no additional functionality
  beyond that of the library.

SEE ALSO
  NetReceiver help: http://netreceiver.appspot.com/help.

LICENSE
  Copyright (C) 2019 the Australian Ocean Lab (AusOcean).

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

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup(void) {
  NetSender::init();
  loop();
}

void loop() {
  while (!NetSender::run(&varsum)) {
    delay(RETRY_PERIOD * (long)1000);
  }
}
