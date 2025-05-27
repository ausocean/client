/*
  Description:
    NetSender offline request handler.

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

#include <stdlib.h>
#include "Arduino.h"

#include "NetSender.h"

namespace NetSender {

// Offline request handler.
// One-time setup.
bool OfflineHandler::init() {
  return true;
}

// Requests are handled as follows:
// - Config & Vars: delegated to the online handler, which will fail unless there is network connectivity.
// - Poll: write data to local storage, which does not require network connectivity.
// - Act: does nothing.
bool OfflineHandler::request(RequestType req, Pin * inputs, Pin * outputs, bool * reconfig, String& reply) {
  switch (req) {
  case RequestConfig:
  case RequestVars:
    {
      auto h = Handlers.get(mode::Online);
      if (h == NULL) {
        return false;
      }
      auto ok = h->request(req, NULL, NULL, reconfig, reply);
      h->disconnect();
      return ok;
    }
  case RequestPoll:
    break; // Handle below.
  case RequestAct:
    return true;
  default:
    return false;
  }

  if (inputs == NULL) {
    return true; // Nothing to do.
  }

  for (int ii = 0; ii < MAX_PINS && inputs[ii].name[0] != '\0'; ii++) {
    if (inputs[ii].value < 0) {
      // Omit negative scalars.
      log(logDebug, "Not saving negative value for %s", inputs[ii].name);
      continue;
    }
    // ToDo: Save value to local storage.
    log(logDebug, "Saving %s=%s", inputs[ii].name, inputs[ii].value);
  }

  return true;
}

} // end namespace
