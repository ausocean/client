/*
NAME
  test-netsender - NetSender test client

AUTHOR
  Alan Noble <alan@ausocean.org>

LICENSE
  Copyright (C) 2018-2019 the Australian Ocean Lab (AusOcean).

  This is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  This is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with revid in gpl.txt.  If not, see [GNU licenses](http://www.gnu.org/licenses).
*/

package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/ausocean/client/pi/netsender"
)

const (
	progName    = "test-netsender"
	version     = "v1.10"
	retryPeriod = 30
	mtsPktSize  = 1316
)

var (
	varsum   int
	bursting bool
	mu       sync.Mutex
	tl       TestLogger
)

func main() {
	configFile := flag.String("ConfigFile", "./netsender.conf", "Specifies NetSender config file")
	burstPeriod := flag.Int("BurstPeriod", 10, "Specifies burst period in seconds (10s by default)")
	flag.Parse()

	tl.Log(netsender.InfoLevel, progName+" "+version)

	fmt.Printf("ConfigFile=%s\n", *configFile)
	ns, err := netsender.New(&tl, nil, testRead, nil, netsender.WithConfigFile(*configFile))
	if err != nil {
		fmt.Printf("netsender.New() returned error %v", err)
		tl.Log(netsender.FatalLevel, "netsender.New() returned failed")
	}

	_, err = ns.Vars()
	if err != nil {
		fmt.Printf("ns.Vars() returned error %v", err)
		tl.Log(netsender.FatalLevel, "ns.Vars() failed")
	}
	varsum = ns.VarSum()

	for {
		// Run handles config and poll requests
		err := ns.Run()
		if err != nil {
			tl.Log(netsender.WarningLevel, "Run failed", "error", err.Error())
		}

		// Send handles mts requests (for video, if any).
		if strings.ContainsAny(ns.Param("ip"), "V0") {
			var pkt [mtsPktSize]byte
			pins := []netsender.Pin{netsender.Pin{Name: "V0", Value: len(pkt), Data: pkt[:], MimeType: "video/mp2t"}}
			_, _, err = ns.Send(netsender.RequestMts, pins[:])
			if err != nil {
				tl.Log(netsender.WarningLevel, "Send failed with error %s", err.Error())
			}
		}

		if err != nil {
			time.Sleep(time.Duration(retryPeriod) * time.Second)
		}

		mu.Lock()
		vs := varsum
		mu.Unlock()
		if vs == ns.VarSum() {
			goto pause
		}
		_, err = ns.Vars()
		if err != nil {
			tl.Log(netsender.FatalLevel, "ns.Vars() failed")
		}
		mu.Lock()
		varsum = ns.VarSum()
		mu.Unlock()

		switch ns.Mode() {
		case "Normal":
			// Continue as is.
		case "Burst":
			tl.Log(netsender.InfoLevel, "Received Burst.")
			mu.Lock()
			b := bursting
			mu.Unlock()
			// Only burst if we are not already bursting.
			if !b {
				go burst(ns, *burstPeriod)
			}
		case "Stop":
			tl.Log(netsender.InfoLevel, "Received Stop. Stopping...")
			os.Exit(0)
		default:
		}

	pause:
		mp := ns.Param("mp")
		val, err := strconv.Atoi(mp)
		if err != nil {
			val = retryPeriod
		}
		tl.Log(netsender.DebugLevel, "Sleeping...")
		time.Sleep(time.Duration(val) * time.Second)
	}
}

// Simulate some burst activity
// Side effects: set bursting to false resets our varsum upon completion.
func burst(ns *netsender.Sender, burstPeriod int) {
	mu.Lock()
	bursting = true
	mu.Unlock()

	tl.Log(netsender.InfoLevel, "Bursting...")
	time.Sleep(time.Duration(burstPeriod) * time.Second)

	mu.Lock()
	tl.Log(netsender.InfoLevel, "Bursting done. Resetting mode to Normal.")
	ns.SetMode("Normal")
	bursting = false
	mu.Unlock()
}

// testRead implements a test pin reader
func testRead(pin *netsender.Pin) error {
	pin.Value = -1
	switch pin.Name {
	case "A0":
		pin.Value = 0
		pin.Data = nil
	case "B0":
		pin.Data = []byte{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}
		pin.Value = len(pin.Data)
		pin.MimeType = "application/octet-stream"
	case "T0":
		msg := `{"level":"info","message":"Hello world"}`
		pin.Value = len(msg)
		pin.Data = []byte(msg)
		pin.MimeType = "application/json"
	case "V0":
		var pkt [mtsPktSize]byte
		pin.Data = pkt[:]
		pin.Value = len(pkt)
		pin.MimeType = "video/mp2t"
	case "X0":
		pin.Value = 1
		pin.Data = nil
	default:
		return errors.New("InvalidPin")
	}
	return nil
}
