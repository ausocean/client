/*
DESCRIPTION
  alignment-netsender is a netsender client responsible for alignment control
  of the CPE router. Compass readings are obtained from a magnetometer and
  and corrections are made using a servo motor. Median delta between readings
  and target bearing are sent to the cloud, and tuning can be performed using
  variables.

AUTHORS
  Saxon A. Nelson-Milton <saxon@ausocean.org>

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

// alignment-netsender is a netsender client responsible for alignment control
// of the CPE router. Compass readings are obtained from a magnetometer and
// and corrections are made using a servo motor. Median delta between readings
// and target bearing are sent to the cloud, and tuning can be performed using
// variables.
package main

import (
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/utils/logging"
	"gopkg.in/natefinch/lumberjack.v2"
)

// Logging configuration.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB.
	logMaxBackup = 10
	logMaxAge    = 28 // Days.
	logVerbosity = logging.Debug
	logSuppress  = false
)

// Sleep times.
const (
	netSendRetryTime = 5 * time.Second
	defaultSleepTime = 60 * time.Second
)

// Software defined pins.
const (
	pinMedianAlignErr = "X23"
	pinErrorStdDev    = "X24"
	pinLinkSignal     = "X25"
	pinLinkQuality    = "X26"
	pinLinkNoise      = "X27"
	pinLinkBitrate    = "X28"
	pinRefAngle       = "X29"
)

// Default link configuration.
const (
	defaultDevice = "wlan0"
	defaultIP     = "192.168.0.1"
	defaultPort   = "22"
	defaultUser   = "admin"
	defaultPass   = "pass"
)

// Information for variables that are sent to cloud and then tunable.
var variables = []struct {
	name   string
	typ    string
	update func(a *CPEAligner, value string) error
}{
	{
		name: "AdjustmentInterval",
		typ:  "uint",
		update: func(a *CPEAligner, v string) error {
			s, err := strconv.Atoi(v)
			if err != nil {
				return fmt.Errorf("could not convert AdjustmentInterval variable value to int: %w", err)
			}
			a.SetAdjustIntvl(s)
			return nil
		},
	},
	{
		name: "SweepIncDelay",
		typ:  "uint",
		update: func(a *CPEAligner, v string) error {
			d, err := strconv.Atoi(v)
			if err != nil {
				return fmt.Errorf("could not convert SweepIncDelay variable value to int: %w", err)
			}
			a.SetSweepIncDelay(d)
			return nil
		},
	},
	{
		name: "Calibrate",
		typ:  "bool",
		update: func(a *CPEAligner, v string) error {
			switch strings.ToLower(v) {
			case "true":
				a.Calibrate()
			case "false":
			default:
				return fmt.Errorf("invalid Calibrate value: %s", v)
			}
			return nil
		},
	},
	{
		name: "Gain",
		typ:  "float",
		update: func(a *CPEAligner, v string) error {
			g, err := strconv.ParseFloat(v, 64)
			if err != nil {
				return fmt.Errorf("could not convert Gain variable value to float: %w", err)
			}
			err = a.SetGain(g)
			if err != nil {
				return fmt.Errorf("could not set Gain: %w", err)
			}
			return nil
		},
	},
	{
		name: "Threshold",
		typ:  "float",
		update: func(a *CPEAligner, v string) error {
			t, err := strconv.ParseFloat(v, 64)
			if err != nil {
				return fmt.Errorf("could not convert Threshold variable value to float: %w", err)
			}
			err = a.SetThreshold(t)
			if err != nil {
				return fmt.Errorf("could not set threshold: %w", err)
			}
			return nil
		},
	},
	{
		name: "LinkConfig",
		typ:  "string",
		update: func(a *CPEAligner, v string) error {
			err := a.SetLinkConfig(v)
			if err != nil {
				return fmt.Errorf("could not set link config: %w", err)
			}
			return nil
		},
	},
	{
		name: "ReferenceAngle",
		typ:  "float",
		update: func(a *CPEAligner, v string) error {
			ra, err := strconv.ParseFloat(v, 64)
			if err != nil {
				return fmt.Errorf("could not convert ReferenceAngle variable value to float: %w", err)
			}
			err = a.SetRefAngle(ra)
			if err != nil {
				return fmt.Errorf("could not set reference angle: %w", err)
			}
			return nil
		},
	},
}

// Uses variables []struct to create a map of name->type format for sending to cloud.
func createVarMap() map[string]string {
	m := make(map[string]string, len(variables))
	for _, v := range variables {
		m[v.name] = v.typ
	}
	return m
}

func main() {
	fileLog := &lumberjack.Logger{
		Filename:   logPath,
		MaxSize:    logMaxSize,
		MaxBackups: logMaxBackup,
		MaxAge:     logMaxAge,
	}

	netLog := netlogger.New()

	log := logging.New(logVerbosity, io.MultiWriter(fileLog, netLog), logSuppress)

	l, err := newLink(defaultDevice, defaultIP, defaultPort, defaultUser, defaultPass)
	if err != nil {
		log.Error("could not create link", "error", err)
	}

	log.Debug("initialising CPE aligner")
	aligner, err := NewCPEAligner(log, l)
	if err != nil {
		log.Fatal("failed to create aligner", "error", err)
	}

	log.Debug("initialising netsender client")
	ns, err := netsender.New(log, nil, readPin(aligner, log), nil)
	if err != nil {
		log.Fatal("could not initialise netsender client: " + err.Error())
	}

	run(aligner, ns, log, netLog)
}

// run starts the main loop. This will run netsender on every pass of the loop
// (sleeping inbetween), check vars, and if changed, update the CPE aligner as appropriate.
func run(aligner *CPEAligner, ns *netsender.Sender, l *logging.JSONLogger, nl *netlogger.Logger) {
	go aligner.Align()

	var vs int
	for {
		l.Debug("running netsender")
		err := ns.Run()
		if err != nil {
			l.Warning("Run Failed. Retrying...", "error", err.Error())
			time.Sleep(netSendRetryTime)
			continue
		}

		l.Debug("sending logs")
		err = nl.Send(ns)
		if err != nil {
			l.Warning("Logs could not be sent", "error", err.Error())
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
			l.Error("netSender failed to get vars", "error", err.Error())
			time.Sleep(netSendRetryTime)
			continue
		}
		l.Debug("got new vars", "vars", vars)

		// Var sum has changed, so loop through variables []struct and use each variables
		// Update func to update the appropriate fields of the aligner.
		for _, value := range variables {
			if v, ok := vars[value.name]; ok && value.update != nil {
				err := value.update(aligner, v)
				if err != nil {
					l.Warning("could not update variable", "name", value.name, "error", err)
				}
			}
		}

		sleep(ns, l)
	}
}

// sleep uses a delay to halt the program based on the monitoring period
// netsender parameter (mp) defined in the netsender.conf config.
func sleep(ns *netsender.Sender, l *logging.JSONLogger) {
	l.Debug("sleeping")
	mp, err := strconv.Atoi(ns.Param("mp"))
	t := time.Duration(mp) * time.Second
	if err != nil {
		l.Error("could not get sleep time, using default", "error", err)
		t = defaultSleepTime
	}
	time.Sleep(t)
	l.Debug("finished sleeping")
}

// readPin provides a callback function of consistent signature for use by
// netsender to retrieve software defined pin values e.g. revid bitrate.
// Here we report the median difference of bearing with target bearing.
func readPin(aligner *CPEAligner, log *logging.JSONLogger) func(pin *netsender.Pin) error {
	return func(pin *netsender.Pin) error {
		pin.Value = -1
		switch pin.Name {
		case pinMedianAlignErr:
			pin.Value = int(math.Round(aligner.MedianError()))
			log.Info("sending median aligner error", "median", pin.Value)
		case pinErrorStdDev:
			// This requires slightly higher resolution than an int. Given that we can
			// only load ints into netsender software defined pins, we'll multiple by
			// 1000 here, and then divide in the cloud to get a float.
			pin.Value = int(math.Round(aligner.ErrorStdDev() * 1000))
			log.Info("sending signal to noise ratio", "snr", pin.Value)
		case pinLinkSignal:
			v, err := aligner.LinkSignal()
			if err != nil {
				log.Error("could not get link signal strength", "error", err)
				return nil
			}
			pin.Value = v
			log.Info("sending signal strength", "strength", pin.Value)
		case pinLinkQuality:
			v, err := aligner.LinkQuality()
			if err != nil {
				log.Error("could not get link quality", "error", err)
				return nil
			}
			pin.Value = v
			log.Info("sending link quality", "quality", pin.Value)
		case pinLinkNoise:
			v, err := aligner.LinkNoise()
			if err != nil {
				log.Error("could not get link noise", "error", err)
				return nil
			}
			pin.Value = v
			log.Info("sending link noise", "noise", pin.Value)
		case pinLinkBitrate:
			v, err := aligner.LinkBitrate()
			if err != nil {
				log.Error("could not get link bitrate", "error", err)
				return nil
			}
			pin.Value = v
			log.Info("sending link bitrate", "bitrate", pin.Value)
		case pinRefAngle:
			pin.Value = int(math.Round(aligner.RefAngle()))
			log.Info("sending aligner reference angle", "angle", pin.Value)
		default:
			log.Warning("unknown pin specified for device", "name", pin.Name)
		}
		return nil
	}
}
