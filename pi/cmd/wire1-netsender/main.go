/*
NAME
  wire1-netsender - NeSender client for the ds18b20 temperature sensor

DESCRIPTION
  See Readme.md

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>

LICENSE
  gpio-netsender is Copyright (C) 2017-2018 the Australian Ocean Lab (AusOcean).

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

// wire1-netsender is a NetSender client for the DS18B20 temperature sensor using the 1-Wire protocol.
package main

import (
	"errors"
	"flag"
	"os"
	"time"

	"github.com/yryz/ds18b20"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/smartlogger"
	"github.com/ausocean/utils/logging"
)

// defaults and networking consts
const (
	progName    = "wire1-netsender"
	logPath     = "/var/log/netsender"
	retryPeriod = 30 * time.Second
)

func ds18b20Read(pin *netsender.Pin) error {
	if pin.Name != "X60" {
		return nil
	}
	sensors, err := ds18b20.Sensors()
	if err != nil || len(sensors) < 1 {
		return errors.New("No DS18B20 sensors connected")
	}

	t, err := ds18b20.Temperature(sensors[0])
	if err != nil {
		return errors.New("Unable to read temperature")
	}
	pin.Value = int(t)
	return nil
}

var log logging.Logger

func main() {
	var logLevel int
	flag.IntVar(&logLevel, "LogLevel", int(logging.Debug), "Specifies log level")
	flag.Parse()

	validLogLevel := true
	if logLevel < int(logging.Debug) || logLevel > int(logging.Fatal) {
		logLevel = int(logging.Info)
		validLogLevel = false
	}

	// Create logger
	logSender := smartlogger.New(logPath)
	log = logging.New(int8(logLevel), &logSender.LogRoller, true)
	log.Info( "log-netsender: Logger Initialized")
	if !validLogLevel {
		log.Error( "Invalid log level was defaulted to Info")
	}

	ns, err := netsender.New(log, nil, ds18b20Read, nil)
	if err != nil {
		log.Error( "Error", err.Error())
		os.Exit(1)
	}

	for {
		if err := ns.Run(); err != nil {
			log.Warning( "Run Failed with: ", "error", err.Error())
			time.Sleep(retryPeriod)
		}
	}
}
