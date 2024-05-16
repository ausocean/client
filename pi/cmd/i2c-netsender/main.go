/*
DESCRIPTION
  i2c-netsender is a netsender client for reporting values obtained via
  I2C from sensors eg. the Atlas Scientific sensors.

AUTHORS
  Trek Hopton <trek@ausocean.org>

LICENSE
  Copyright (C) 2021 the Australian Ocean Lab (AusOcean)

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  in gpl.txt.  If not, see http://www.gnu.org/licenses.
*/

// i2c-netsender is a program that implements a netsender client for reporting of I2C sensor readings.
package main

import (
	"errors"
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/kidoman/embd"
	_ "github.com/kidoman/embd/host/rpi"
	lumberjack "gopkg.in/natefinch/lumberjack.v2"

	"github.com/ausocean/client/pi/gpio"
	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/utils/logging"
)

// Logging configuration.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB
	logMaxBackup = 10
	logMaxAge    = 28 // days
	logVerbosity = logging.Debug
	logSuppress  = true
)

// Netsender timeouts.
const (
	netSendRetryTime = 5 * time.Second
	defaultSleepTime = 60 // Seconds
)

// Software defined pins for netsender and cloud use.
const (
	salinityPin    = "X35"
	dissolvedO2Pin = "X37" // Dissolved Oxygen.
)

// I2C sensor addresses.
const (
	salinityAddr    = 0x64
	dissolvedO2Addr = 0x61 // Dissolved Oxygen.
)

// I2C sensor values.
const (
	i2cPort      = 1
	i2cReadDelay = 600
	i2cCmd       = "R"
	minResponse  = 3
	maxResponse  = 40
	successCode  = 1
)

// Multiplication factor used to preserve accuracy when converting a float to an integer for use with netsender pins.
// Here a value of 1000 will preserve 3 decimal points of accuracy.
// This is the number that the data should be divided by in order to obtain the original float value.
const floatAccuracy = 1000

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

	// The netsender client will handle communication with netreceiver and GPIO stuff.
	log.Debug("initialising netsender client")
	ns, err := netsender.New(log, gpio.InitPin, readPin(log), gpio.WritePin, nil)
	if err != nil {
		log.Fatal("could not initialise netsender client", "error", err)
	}

	// Start the control loop.
	log.Debug("starting control loop")
	run(ns, log, netLog)
}

// run starts a control loop that runs netsender, sends logs, checks for var changes.
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
			l.Warning("Logs could not be sent", "error", err)
		}

		l.Debug("checking varsum")
		newVs := ns.VarSum()
		if vs == newVs {
			sleep(ns, l)
			continue
		}
		vs = newVs
		l.Info("varsum changed", "vs", vs)

		l.Debug("getting new vars")
		vars, err := ns.Vars()
		if err != nil {
			l.Error("netSender failed to get vars", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}
		l.Info("got new vars", "vars", vars)

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
// netsender to read and update software defined pin values.
func readPin(l logging.Logger) func(pin *netsender.Pin) error {
	bus := embd.NewI2CBus(i2cPort)
	return func(pin *netsender.Pin) error {
		switch pin.Name {
		case salinityPin:
			err := readSalinity(pin, bus, l)
			if err != nil {
				return fmt.Errorf("error reading from salinity sensor: %w", err)
			}
		case dissolvedO2Pin:
			err := readDO(pin, bus, l)
			if err != nil {
				return fmt.Errorf("error reading from dissolved oxygen sensor: %w", err)
			}
		}
		return nil
	}
}

func readSalinity(pin *netsender.Pin, bus embd.I2CBus, l logging.Logger) error {
	if pin.Name != salinityPin {
		return errors.New("provided pin is not for salinity")
	}
	ms, err := readI2C(pin, bus, salinityAddr, l)
	if err != nil {
		return err
	}
	// At the levels of conductance we'll be dealing with in the ocean, this sensor doesn't provide decimal point resolution so we can use integers.
	l.Info(fmt.Sprintf("read conductance of %v microsiemens, rounding to integer", ms))
	pin.Value = int(math.RoundToEven(ms))
	return nil
}

func readDO(pin *netsender.Pin, bus embd.I2CBus, l logging.Logger) error {
	if pin.Name != dissolvedO2Pin {
		return errors.New("provided pin is not for dissolved oxygen")
	}
	do, err := readI2C(pin, bus, dissolvedO2Addr, l)
	if err != nil {
		return err
	}
	l.Info(fmt.Sprintf("read %v mg/L, multiplying by %v then rounding to integer", do, floatAccuracy))
	pin.Value = int(math.RoundToEven(do * floatAccuracy))
	return nil
}

func readI2C(pin *netsender.Pin, bus embd.I2CBus, addr byte, l logging.Logger) (float64, error) {
	err := bus.WriteBytes(addr, []byte(i2cCmd))
	if err != nil {
		return 0, fmt.Errorf("failed to write command to I2C device: %w", err)
	}
	time.Sleep(time.Duration(i2cReadDelay) * time.Millisecond)
	bytes, err := bus.ReadBytes(addr, maxResponse)
	if err != nil {
		return 0, fmt.Errorf("failed to read I2C device: %w", err)
	}
	r, code, err := parseResponse(bytes)
	if err != nil {
		return 0, fmt.Errorf("could not parse response: %w", err)
	}
	if code != successCode {
		l.Warning("error code in response", "code", code)
	}
	return r, nil
}

// parseResponse parses a given byte slice containing an I2C reponse from an Altlas Scientific sensor.
// For example, see https://atlas-scientific.com/files/EC_EZO_Datasheet.pdf page 49 for the response format.
// The response is returned as a float64 with the integer response code.
// If an error occurs, the error will be returned with the response and response code both set to -1.
func parseResponse(bytes []byte) (float64, int, error) {
	n := len(bytes)
	if n < minResponse || maxResponse < n {
		return -1, -1, fmt.Errorf("wrong number of bytes in response, should be %d < n < %d, but contains %d", minResponse, maxResponse, n)
	}
	code := int(bytes[0])
	valueStr := strings.TrimRight(string(bytes[1:]), "\x00")
	ms, err := strconv.ParseFloat(valueStr, 64)
	if err != nil {
		return -1, -1, fmt.Errorf("could not parse float from response: %w", err)
	}
	return ms, code, nil
}
