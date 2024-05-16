/*
DESCRIPTION
  router-netsender is a netsender client implementation that reports statistics about
  a given router to our cloud services.

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

// router-netsender is a netsender client implementation that reports statistics about
// a given router to our cloud services.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"strconv"
	"strings"
	"time"

	_ "github.com/kidoman/embd/host/rpi"
	"gopkg.in/natefinch/lumberjack.v2"

	"github.com/ausocean/client/pi/netlogger"
	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/remote"
	"github.com/ausocean/utils/logging"
)

// Logging configuration.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB
	logMaxBackup = 10
	logMaxAge    = 28 // days
	logVerbosity = logging.Debug
	logSuppress  = false
)

// Shell commands.
const (
	syslogCmd = "logread"
	dmesgCmd  = "dmesg -c"
	pingCmd   = "ping -c 8 8.8.8.8"
	uptimeCmd = "uptime"
	topCmd    = "top -n 1"
)

// Log messages.
const (
	syslogRead = "existing remote syslog read"
	dmesgRead  = "existing remote dmesg log read"
)

// Netsender timeouts.
const (
	netSendRetryTime = 5 * time.Second
	defaultSleepTime = 60 // Seconds
	remoteCmdTime    = 20 * time.Second
)

// Option defaults.
const (
	defaultUser         = "root"
	defaultPassword     = "admin"
	defaultLocalIP      = "192.168.8.16"
	defaultRemoteIP     = "192.168.8.1"
	defaultReadExisting = true
)

func main() {
	user := flag.String("user", defaultUser, "Username for remote machine.")
	pass := flag.String("password", defaultPassword, "Password for given user on remote machine.")
	local := flag.String("local", defaultLocalIP, "Local host IP to listen to for incoming TCP connections.")
	remoteIP := flag.String("remote", defaultRemoteIP, "Remote router IP to connect to via SSH.")
	readExist := flag.Bool("read-exist", defaultReadExisting, "Set true to perform initial reading of existing logs.")
	flag.Parse()

	// Create loggers to handle logging to file and to the cloud.
	fileLog := &lumberjack.Logger{
		Filename:   logPath,
		MaxSize:    logMaxSize,
		MaxBackups: logMaxBackup,
		MaxAge:     logMaxAge,
	}
	netLog := netlogger.New()
	l := logging.New(logVerbosity, io.MultiWriter(fileLog, netLog), logSuppress)

	router := remote.New(*user, *pass, *remoteIP)

	// The netsender client will handle communication with netreceiver.
	l.Debug("initialising netsender client")
	ns, err := netsender.New(l, nil, readPin(l, router), nil, nil)
	if err != nil {
		l.Fatal("could not initialise netsender client", "error", err)
	}

	// Initial reading of logs that already exist.
	if *readExist {
		err = readExisting(router, l)
		if err != nil {
			l.Error("error reading existing logs", "error", err)
		}
	}

	// Start listening for new syslogs.
	go remote.Listen(l, *local)

	// Start the control loop.
	l.Debug("starting control loop")
	run(ns, router, l, netLog)
}

// readExisting logs into a remote target via SSH and writes logs into the given logger.
func readExisting(router *remote.Remote, l logging.Logger) error {
	l.Info("performing read of existing remote logs")

	// Establish SSH connection with the router.
	err := router.Connect()
	if err != nil {
		return fmt.Errorf("failed to connect to router: %w", err)
	}
	defer func() {
		err := router.Disconnect()
		if err != nil {
			l.Error("disconnecting from router failed", "error", err)
		}
	}()

	// Read existing syslogs.
	out, err := router.Exec(syslogCmd, remoteCmdTime)
	if err != nil {
		return fmt.Errorf("failed to execute syslog command: %w", err)
	}
	loglines(out, l, syslogRead)

	// Read existing dmesg logs.
	err = logDmesg(router, l)
	if err != nil {
		return fmt.Errorf("could not log dmesg: %w", err)
	}

	return nil
}

// loglines takes a multiline string of text and logs the lines individually
// to the given logger along with the given message.
func loglines(text string, l logging.Logger, msg string) {
	lines := strings.Split(text, "\n")
	for _, line := range lines {
		if line != "" {
			l.Info(msg, "text", line)
		}
	}
}

// logDmesg executes the dmesg command on the router and logs the output to the given logger.
// This function assumes that router.Connect has been called and will fail otherwise.
func logDmesg(router *remote.Remote, l logging.Logger) error {
	out, err := router.Exec(dmesgCmd, remoteCmdTime)
	if err != nil {
		return fmt.Errorf("failed to execute dmesg command: %w", err)
	}
	loglines(out, l, dmesgRead)
	return nil
}

// run starts a control loop that runs netsender, checks for var changes, performs any updates with new variables, sends logs.
func run(ns *netsender.Sender, router *remote.Remote, l logging.Logger, nl *netlogger.Logger) {
	for {
		err := ns.TestDownload()
		if err != nil {
			l.Error("could not test download speed: %w", err)
		}

		err = ns.TestUpload()
		if err != nil {
			l.Error("could not test upload speed: %w", err)
		}

		l.Debug("running netsender")
		err = ns.Run()
		if err != nil {
			l.Warning("run failed, retrying...", "error", err)
			time.Sleep(netSendRetryTime)
			continue
		}

		err = logRouterDmesg(router, ns, l)
		if err != nil {
			l.Error("could not get router logs", "error", err)
		}

		err = nl.Send(ns)
		if err != nil {
			l.Error("could not send logs to cloud", "error", err)
		}

		sleep(ns, l)
	}
}

// logRouterDmesg reads any new dmesg logs from the router into the given logger.
func logRouterDmesg(router *remote.Remote, ns *netsender.Sender, l logging.Logger) error {
	l.Debug("adding dmesg logs for the past monitor period", "mp", ns.Param("mp"))
	err := router.Connect()
	if err != nil {
		return fmt.Errorf("could not connect to router via SSH: %w", err)
	}
	defer func() {
		err = router.Disconnect()
		if err != nil {
			l.Error("disconnecting from router failed", "error", err)
		}
	}()
	err = logDmesg(router, l)
	if err != nil {
		return fmt.Errorf("could not read dmesg logs: %w", err)
	}
	return nil
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
func readPin(l logging.Logger, router *remote.Remote) func(pin *netsender.Pin) error {
	return func(pin *netsender.Pin) error {
		switch pin.Name {
		case "T3":
			err := router.Connect()
			if err != nil {
				return fmt.Errorf("could not connect to router: %w", err)
			}
			defer func() {
				err = router.Disconnect()
				if err != nil {
					l.Error("disconnecting from router failed", "error", err)
				}
			}()

			m := make(map[string]string)

			l.Debug("executing ping command on router")
			out, err := router.Exec(pingCmd, remoteCmdTime)
			if err != nil {
				return fmt.Errorf("failed to run ping command on router: %w", err)
			}
			m["ping"] = out

			l.Debug("executing uptime command on router")
			out, err = router.Exec(uptimeCmd, remoteCmdTime)
			if err != nil {
				return fmt.Errorf("failed to run uptime command on router: %w", err)
			}
			m["uptime"] = out

			l.Debug("executing top command on router")
			out, err = router.Exec(topCmd, remoteCmdTime)
			if err != nil {
				return fmt.Errorf("failed to run top command on router: %w", err)
			}
			m["top"] = out

			j, err := json.Marshal(m)
			if err != nil {
				return fmt.Errorf("failed to marshal: %w", err)
			}
			pin.Value = len(j)
			pin.Data = j
			pin.MimeType = "application/json"
		}
		return nil
	}
}
