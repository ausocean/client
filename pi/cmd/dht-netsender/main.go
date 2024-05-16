/*
	CURRENTLY BROKEN

	go-dht is a broken dependancy
*/

/*
NAME
  dht-netsender - NetSender client for the DHT-11 or DHT-22 humidity and temperature sensor

DESCRIPTION
  See Readme.md

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>
  Alan Noble <alan@ausocean.org>

LICENSE
  dht-netsender is Copyright (C) 2017-2018 the Australian Ocean Lab (AusOcean).

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

// dht-netsender is a NetSender client for  the DHT-11 or DHT-22 humidity and temperature sensor.
package main

import (
	"errors"
	"flag"
	"os/user"
	"strconv"
	"time"

	//TODO: Investigate broken dependancy
	dht "github.com/d2r2/go-dht"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/smartlogger"
	"github.com/ausocean/utils/filemap"
	"github.com/ausocean/utils/logging"
)

// defaults
const (
	progName    = "dht-netsender"
	logPath     = "/var/log/netsender"
	retryPeriod = 30 // seconds
	dht11Temp   = "X40"
	dht11Hum    = "X41"
	dht22Temp   = "X50"
	dht22Hum    = "X51"
)

var log logging.Logger

// DHT pin
var dhtPin int = 22

//dhtRead reads and interprets humidity and temperature data from a DHT sensor
func dhtRead(pin *netsender.Pin) error {
	var val float32
	var err error

	switch pin.Name {
	case dht11Temp:
		val, _, _, err = dht.ReadDHTxxWithRetry(dht.DHT11, dhtPin, true, 5)

	case dht11Hum:
		_, val, _, err = dht.ReadDHTxxWithRetry(dht.DHT11, dhtPin, true, 5)

	case dht22Temp:
		val, _, _, err = dht.ReadDHTxxWithRetry(dht.DHT22, dhtPin, true, 5)

	case dht22Hum:
		_, val, _, err = dht.ReadDHTxxWithRetry(dht.DHT22, dhtPin, true, 5)

	default:
		pin.Value = -1
		return errors.New("External pin not defined")
	}
	if err != nil {
		pin.Value = -1
		return errors.New("DHT read error: " + err.Error())
	}
	pin.Value = int(val) * 10
	return nil
}

func main() {
	var logLevel int
	flag.IntVar(&logLevel, "LogLevel", int(logging.Debug), "Specifies log level")
	flag.Parse()

	validLogLevel := true
	if logLevel < int(logging.Debug) || logLevel > int(logging.Fatal) {
		logLevel = int(logging.Info)
		validLogLevel = false
	}

	logSender := smartlogger.New(logPath)
	log = logging.New(int8(logLevel), &logSender.LogRoller, true)
	log.Info("log-netsender: Logger Initialized")
	if !validLogLevel {
		log.Error("Invalid log level was defaulted to Info")
	}

	user, _ := user.Current()
	if user.Uid != "0" {
		log.Error("Must run as root")
		return
	}
	ns, err := netsender.New(log, nil, dhtRead, nil)
	if err != nil {
		log.Error(err.Error())
		return
	}

	// dhtPin may be specified in netsender.conf via the hw config param
	hwConfig := filemap.Split(ns.Param("hw"), ",", "=")
	if val, err := strconv.Atoi(hwConfig["dhtPin"]); err != nil {
		log.Error("Error", err.Error())
		return
	} else {
		dhtPin = val
	}

	log.Info("dht Pin", "pin number", strconv.Itoa(dhtPin))

	for {
		if err := ns.Run(); err != nil {
			log.Warning("Run Failed", "error", err.Error())
			time.Sleep(time.Duration(retryPeriod) * time.Second)
		}
	}
}
