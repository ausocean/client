/*
DESCRIPTION
  turbidity-netsender is a netsender client responsible for reading the Remond
  Modbus Turbidity Sensor. Turbidity readings are obtained from the sensor via
  an Arduino Uno to a Raspberry Pi Zero via USB.

AUTHORS
  Harrison Telford <harrison@ausocean.org>
  Saxon Nelson-Milton <saxon@ausocean.org>

LICENSE
  Copyright (C) 2020-2021 the Australian Ocean Lab (AusOcean)

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with revid in gpl.txt. If not, see http://www.gnu.org/licenses.
*/

// turbidity-netsender is a netsender client responsible for reading the Remond
// Modbus turbidity sensor. Turbidity sensor readings are obtained through the
// ArduinoModbus library on an Arduino Uno and read by a Raspberry Pi via USB
// through a python script.
package main

import (
	"io"
	"strconv"
	"time"

	"gopkg.in/natefinch/lumberjack.v2"

	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/utils/logging"
)

// Logging configuration conts.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB.
	logMaxBackup = 10
	logMaxAge    = 28 // Days.
	logVerbosity = logging.Debug
	logSuppress  = false
)

// Misc constants.
const (
	netSendRetryTime = 5 * time.Second
	defaultSleepTime = 60 // Seconds.
	runPreDelay      = 20 * time.Second
	turbidityPin     = "X34"
)

func main() {
	fileLog := &lumberjack.Logger{
		Filename:   logPath,
		MaxSize:    logMaxSize,
		MaxBackups: logMaxBackup,
		MaxAge:     logMaxAge,
	}

	netLog := netlogger.New()

	log := logging.New(logVerbosity, io.MultiWriter(fileLog, netLog), logSuppress)

	log.Debug("initialising turbidity sensor")
	sensor, err := NewTurbiditySensor(log)
	if err != nil {
		log.Fatal("failed to create turbidity sensor", "error", err)
	}

	log.Debug("initialising netsender client")
	ns, err := netsender.New(log, nil, readPin(sensor, log), nil)
	if err != nil {
		log.Fatal("could not initialise netsender client", "error", err)
	}

	run(ns, log, netLog)
}

// run starts the main loop. This will run netsender on every pass of the loop
// (sleeping inbetween), check vars, and if changed, update as appropriate.
func run(ns *netsender.Sender, l logging.Logger, nl *netlogger.Logger) {
	var vs int
	for {
		l.Debug("running netsender")
		err := ns.Run()
		if err != nil {
			l.Warning("run failed, retrying", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}

		l.Debug("sending logs")
		err = nl.Send(ns)
		if err != nil {
			l.Warning("Logs could not be sent", "error", err)
		}

		l.Debug("checking VarSum")
		newVs := ns.VarSum()
		if vs == newVs {
			sleep(ns, l)
			continue
		}
		vs = newVs
		l.Info("VarSum changed", "vs", vs)

		l.Debug("getting new vars")
		vars, err := ns.Vars()
		if err != nil {
			l.Error("netSender failed to get vars", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}
		l.Debug("got new vars", "vars", vars)

		sleep(ns, l)
	}
}

// sleep uses a delay to halt the program based on the monitoring period
// netsender parameter (mp) defined in the netsender.conf config.
func sleep(ns *netsender.Sender, l logging.Logger) {
	l.Debug("sleeping")
	t, err := strconv.Atoi(ns.Param("mp"))
	if err != nil {
		l.Error("could not get sleep time, using default", "error", err)
		t = defaultSleepTime
	}
	time.Sleep(time.Duration(t) * time.Second)
	l.Debug("finished sleeping")
}

// readPin provides a callback function of consistent signature for use by
// netsender to retrieve software defined pin values e.g. revid bitrate.
// Here we report the value read by the turbidity sensor.
func readPin(ts *TurbiditySensor, log logging.Logger) func(pin *netsender.Pin) error {
	return func(pin *netsender.Pin) error {
		if pin.Name == turbidityPin {
			pin.Value = -1
			t := ts.Turbidity()
			if t != -1 {
				pin.Value = t
			}
		}
		return nil
	}
}
