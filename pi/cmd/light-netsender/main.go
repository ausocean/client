/*
DESCRIPTION
	Light-netsender is a netsender client intended to control power to a single colour LED
	light in the camera housing using a Raspberry Pi Zero GPIO.
	The use has been originally developed to control UVC for anti-fouling, but can now be extended to other needs.

AUTHORS
	Ella Marie Di Stasio <ellamarie@ausocean.org>
	Trek Hopton <trek@ausocean.org>

LICENSE
	Copyright (C) 2023 the Australian Ocean Lab (AusOcean)
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

// Light-netsender is a program for controlling a light-emitting diode (LED)
// using a RaspberryPi GPIO pin/s through variable on VidGrind.
package main

import (
	"io"
	"strconv"
	"time"

	"github.com/ausocean/client/pi/gpio"
	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/utils/logging"
	_ "github.com/kidoman/embd/host/rpi"
	lumberjack "gopkg.in/natefinch/lumberjack.v2"
)

const (
	netSendRetryTime = 5 * time.Second
	defaultSleepTime = 60 // Seconds
	pkg              = "light-netsender: "
)

// Logging configuration.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB
	logMaxBackup = 10
	logMaxAge    = 28 // days
	logVerbosity = logging.Info
	logSuppress  = true
)

// Light modes.
const (
	modeOff      = "Off"
	modeOn       = "On"
	modeFlashing = "Flashing"
)

// Variable map to send to VidGrind.
var varMap = map[string]string{"lightFlashingMode": "enum:" + modeOff + "," + modeOn + "," + modeFlashing}

func main() {
	// Create lumberjack logger to handle logging to file.
	fileLog := &lumberjack.Logger{
		Filename:   logPath,
		MaxSize:    logMaxSize,
		MaxBackups: logMaxBackup,
		MaxAge:     logMaxAge,
	}

	// Create netlogger to handle logging to cloud.
	netLog := netlogger.New()

	// Create logger that we call methods on to log, which in turn writes to the
	// lumberjack and netloggers.
	log := logging.New(logVerbosity, io.MultiWriter(fileLog, netLog), logSuppress)

	// The netsender client will handle communication with netreceiver and GPIO control.
	log.Debug("initialising netsender client")
	ns, err := netsender.New(log, gpio.InitPin, nil, gpio.WritePin, netsender.WithVarTypes(varMap))
	if err != nil {
		log.Fatal("could not initialise netsender client", "error", err)
	}

	// Start the control loop.
	log.Debug("starting control loop")
	run(ns, log, netLog)
}

// run starts a control loop that runs netsender, sends logs, checks for var changes, and
// if var changes, changes current lightFlashingMode (Off, On, Flashing).
func run(ns *netsender.Sender, l logging.Logger, nl *netlogger.Logger) {
	var vs int
	for {
		l.Debug("running netsender")
		err := ns.Run()
		if err != nil {
			l.Warning("run failed. Retrying...", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}

		l.Debug("sending logs")
		err = nl.Send(ns)
		if err != nil {
			l.Warning(pkg+"logs could not be sent", "error", err)
		}

		l.Debug("checking varsum")
		newVs := ns.VarSum()
		if vs == newVs {
			sleep(ns, l)
			continue
		}
		vs = newVs
		l.Info(pkg+"varsum changed", "vs", vs)

		l.Debug("getting new vars")
		vars, err := ns.Vars()
		if err != nil {
			l.Error(pkg+"netSender failed to get vars", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}
		l.Info(pkg+"got new vars", "vars", vars)

		modePin, modePinOk := vars["lightModePin"]
		mode, flashingModeOk := vars["lightFlashingMode"]
		if !modePinOk || !flashingModeOk {
			l.Info(pkg+"either lightModePin or lightFlashingMode doesn't exist, sleeping", "error", err)
			sleep(ns, l)
			continue
		}

		if modePin == "" || mode == "" {
			l.Warning(pkg+"either lightModePin or lightFlashingMode is empty, sleeping", "error", err)
			sleep(ns, l)
			continue
		}

		p := &netsender.Pin{Name: modePin}
		err = gpio.InitPin(p, netsender.PinOut)
		if err != nil {
			l.Error(pkg+"failed to initialise pin", "error", err)
			sleep(ns, l)
			continue
		}

		// Checking lightFlashingMode from VidGrind and changing pin for different cases.
		switch mode {
		case modeOff:
			p.Value = 0
			err = gpio.WritePin(p)
			if err != nil {
				l.Error(pkg+"error writing 0 to pin", "pin", p.Name, "error", err)
				sleep(ns, l)
				continue
			}
			l.Info(pkg+"pin turned off", "pin", p.Name)
		case modeOn:
			p.Value = 1
			err = gpio.WritePin(p)
			if err != nil {
				l.Error(pkg+"error writing 1 to pin", "pin", p.Name, "error", err)
				sleep(ns, l)
				continue
			}
			l.Info(pkg+"pin turned on", "pin", p.Name)
		case modeFlashing:
			// TODO: implement flashing mode.
			l.Warning(pkg+"modeFlashing is not implemented yet and is not valid", "lightFlashingMode", mode)
		default:
			l.Warning(pkg+"mode is not valid", "lightFlashingMode", mode)
		}
		sleep(ns, l)
	}
}

// sleep uses a delay to halt the program based on the monitoring period
// netsender parameter (mp) defined in the netsender.conf config.
func sleep(ns *netsender.Sender, l logging.Logger) {
	l.Debug("sleeping")
	t, err := strconv.Atoi(ns.Param("mp"))
	if err != nil {
		l.Error(pkg+"could not get sleep time, using default", "error", err)
		t = defaultSleepTime
	}
	time.Sleep(time.Duration(t) * time.Second)
	l.Debug("finished sleeping")
}
