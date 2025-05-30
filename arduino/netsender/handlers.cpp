/*
  Description:
    NetSender handler manager.

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

#include "NetSender.h"

namespace NetSender {

HandlerManager::HandlerManager() {
  size = 0;
  current = 0;
}

HandlerManager::~HandlerManager() {
  for (size_t ii; ii < size; ii++) {
    delete handlers[ii];
  }
  size = 0;
}

// add adds a handler and initializes it.
bool HandlerManager::add(BaseHandler* handler) {
  log(logDebug, "Adding handler %s", handler->name());
  if (size == MAX_HANDLERS) {
    return false;
  }
  handlers[size++] = handler;
  return handler->init();
}

// set sets the current/active handler.
BaseHandler* HandlerManager::set(const char* name) {
  for (size_t ii = 0; ii < size; ii++) {
    if (strcmp(handlers[ii]->name(), name) == 0) {
      log(logDebug, "Set %s handler", name);
      current = ii;
      return handlers[current];
    }
  }
  return NULL;
}

// get() gets the current/active handler.
BaseHandler* HandlerManager::get() {
  if (current >= size) {
    return NULL;
  }
  return handlers[current];
}

// get(name) gets the handler with the given name.
BaseHandler* HandlerManager::get(const char* name) {
  for (size_t ii = 0; ii < size; ii++) {
    if (strcmp(handlers[ii]->name(), name) == 0) {
      return handlers[ii];
    }
  }
  return NULL;
}

} // end namespace
