/*
NAME
  gpio-netsender - netsender client for for analog (A) and GPIO digital (D) sensors.

DESCRIPTION
  See Readme.md

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>
  Alan Noble <alan@ausocean.org>

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

// gpio-netsender is a NetSender client for analog (A) and GPIO digital (D) sensors.
package main

import (
	"flag"
	"io"
	"os"
	"time"

	_ "github.com/kidoman/embd/host/all"
	"gopkg.in/natefinch/lumberjack.v2"

	"github.com/ausocean/client/pi/gpio"
	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/sds"
	"github.com/ausocean/utils/logging"
)

// defaults and networking consts
const (
	progName        = "gpio-netsender"
	retryPeriod int = 30 // seconds
)

func main() {
	var hardware bool
	flag.BoolVar(&hardware, "hardware", false, "Enable hardware peripherals")
	var debug bool
	flag.BoolVar(&debug, "debug", false, "Enable debug logging")
	flag.Parse()

	// Logging configuration.
	const (
		logPath      = "/var/log/netsender/netsender.log"
		logMaxSize   = 500 // MB
		logMaxBackup = 10
		logMaxAge    = 28 // days
		logSuppress  = true
	)
	var logVerbosity = logging.Info
	if debug {
		logVerbosity = logging.Debug
	}

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

	var init netsender.PinInit
	var read netsender.PinReadWrite = sds.ReadSystem
	var write netsender.PinReadWrite

	if hardware {
		init = gpio.InitPin
		read = gpio.ReadPin
		write = gpio.WritePin
		log.Info("gpio-netsender: Initialized hardware")
	}

	ns, err := netsender.New(log, init, read, write)
	if err != nil {
		log.Error("gpio-netsender: Init failed", "error", err.Error())
		os.Exit(1)
	}
	ns.Vars() // updates var sum
	vs := ns.VarSum()
	for {
		if err := ns.Run(); err != nil {
			log.Warning("gpio-netsender: Run Failed", "error", err.Error())
			time.Sleep(time.Duration(retryPeriod) * time.Second)
			continue
		}
		if vs != ns.VarSum() {
			vars, err := ns.Vars()
			if err != nil {
				log.Warning("gpio-netsender: Vars Failed with", "error", err.Error())
				continue
			}
			vs = ns.VarSum()
			if vars["mode"] == "Stop" {
				log.Info("gpio-netsender: Received Stop mode. Stopping...")
				break
			}
		}
	}
}

// TODO(Alan): Implement hardware abstraction layer. The following is just a strawman.
// NB: These all take a pin number, not a Pin.
type hal interface {
	SetDirection(pn int, dir int) error      // Set a digital pin direction.
	DigitalWrite(pn int, val int) error      // Write a digital pin with the given value.
	DigitalRead(pn int) (val int, err error) // Read a digital pin and return the value.
	AnalogRead(pn int) (val int, err error)  // Read an analog pin and return the value.
}
