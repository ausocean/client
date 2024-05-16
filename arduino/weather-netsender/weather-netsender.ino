/*
NAME
  Weather-NetSender - an Arduino program to sender SDL Weather Rack
  data to the cloud using NetSender.

DESCRIPTION

  SDLWeather is a version of the SwitchDocs Labs SDL_Weather_80422
  library modified for the ESP8266.  See SDLWeather.cpp for a
  description of the changes.

  The Weather Rack is configured via the SDLWeather constructor, as follows:

    SDLWeather WeatherStation(4, 5, A0);

  This specifies that the WeatherPi Arduino Weather Board is interfaced to the ESP8266 as follows:
  
    Anemometer is connected to GPIO 4
    Rain bucket is connected to GPIO 5
    Wind vane is connected to A0 (via a step-down voltage divider)

  Note that the first two arguments can be any GPIO pins capable of servicing interrupts.

  Weather-netsender uses "external" pins to collect Weather Rack Data, mapped as follows:

    X30 = True Wind Speed (TWS)
    X31 = True Wind Gust (TWG)
    X32 = True Wind Angle (TWA)
    X33 = Total rainfall (Precipitation) (PPT)

  Note that the total rainfall is reset to zero upon startup.

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
#include "SDLWeather.h"

SDLWeather WeatherStation(4, 5, A0);
float RainTotal = 0.0;
int varsum;

int weatherReader(NetSender::Pin *pin) {
  pin->value = -1;
  if (pin->name[0] != 'X') {
    return -1;
  }
  int pn = atoi(pin->name + 1);
  switch(pn) {
  case 30: // TWS
    pin->value = 10 * WeatherStation.getWindSpeed();
    break;
  case 31: // TWG
    pin->value = 10 * WeatherStation.getWindGust();
    break;
  case 32: // TWA
    pin->value = 10 * WeatherStation.getWindDirection();
    break;
  case 33: // PPT
    RainTotal += WeatherStation.getCurrentRainTotal();
    pin->value = 10 * RainTotal;
    break;
  default:
    return -1;
  }
  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup(void) {
  NetSender::ExternalReader = &weatherReader;
  NetSender::init();
  loop();
}

void loop() {
  while (!NetSender::run(&varsum)) {
    delay(RETRY_PERIOD * (long)1000);
  }
}
