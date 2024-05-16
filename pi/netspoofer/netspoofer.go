/*
NAME
	netspoofer - netspoofer simulates netreceiver responses for the purpose of testing. It currently only supports log receiving.

DESCRIPTION
	See Readme.md

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>

LICENSE
  netspoofer.go is Copyright (C) 2017-2018 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with revid in gpl.txt.  If not, see [GNU licenses](http://www.gnu.org/licenses).
*/

package netspoofer

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"sync"
)

const testPin = "T0"

var (
	pins    = []string{testPin}
	storage string
	mutex   = &sync.Mutex{}
)

// Run starts up server and listens for requests.
func Run() {
	http.HandleFunc("/poll", pollHandler)
	err := http.ListenAndServe("localhost:8000", nil)
	if err != nil {
		log.Fatalf("Httpserver: ListenAndServe() error: %s\n", err)
	}
}

// Logs returns all logs recieved by server.
func Logs() string {
	mutex.Lock()
	defer mutex.Unlock()
	return storage
}

// Reset deletes logs on server.
func Reset() {
	mutex.Lock()
	storage = ""
	mutex.Unlock()
}

// pollHandler handles a poll request from a client. It currently only acts upon pin T0 (log files).
func pollHandler(w http.ResponseWriter, r *http.Request) {
	if r.Body == nil {
		writeError(w, "InvalidPayloadSize")
		return
	}
	defer r.Body.Close()

	r.ParseForm()

	response := map[string]interface{}{
		"rc": 0,
		"vs": 0,
	}

	var found bool
	for _, pin := range pins {
		if pin == testPin {
			found = true
			break
		}
	}
	if !found {
		writeError(w, "PinNotFound")
		return
	}

	val := r.FormValue(testPin)
	if val == "" {
		writeError(w, "InvalidValue")
		return
	}
	response[testPin] = val

	log, err := ioutil.ReadAll(r.Body)
	if err != nil {
		writeError(w, "ReadError")
		return
	}

	mutex.Lock()
	storage += string(log)
	mutex.Unlock()

	response["ma"] = r.FormValue("ma")

	data, err := json.Marshal(response)
	if err != nil {
		writeError(w, "MarshalingError")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Write(data)
}

// writeError writes a JSON response containing a NetReceiver error code.
func writeError(w http.ResponseWriter, er string) {
	w.Header().Add("Content-Type", "application/json")
	fmt.Fprint(w, `{"er":"`+er+`"}`)
}
