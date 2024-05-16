/*
AUTHOR
  Alan Noble <alan@ausocean.org>

LICENSE
  This software is Copyright (C) 2018 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with netsender in gpl.txt.  If not, see [GNU licenses](http://www.gnu.org/licenses).
*/

// Package sds implements standard software-defined sensors (SDS) for the Raspberry Pi.
package sds

import (
	"errors"
	"io/ioutil"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/ausocean/client/pi/netsender"
)

// Errors specific to the sds package:
var (
	ErrUnimplemented   = errors.New("Unimplemented")
	ErrParsingCpuTemp  = errors.New("Error parsing CPU temperature")
	ErrParsingCpuUsage = errors.New("Error parsing CPU usage")
)

// CPU stats reported by /proc/stat
const (
	cpuNumber = iota
	cpuUser
	cpuNice
	cpuSystem
	cpuIdle
	cpuIOWait
	cpuIRQ
	cpuSoftIRQ
	cpuSteal
	cpuGuest
	cpuGuestNice
	cpuMaxStats
)

// ReadSystem implements netsender.PinRead for system information about the Raspberry Pi.
//  X20 - CPU temperature determined by /opt/vc/bin/vcgencmd.
//  X21 - CPU usage determined by read /proc/stat.
//  X22 - Virtual memory (kB) as returned by runtime.ReadMemStats.
func ReadSystem(pin *netsender.Pin) error {
	var val float64
	pin.Value = -1
	pin.Data = nil
	switch pin.Name {
	case "X20":
		out, err := exec.Command("/opt/vc/bin/vcgencmd", "measure_temp").Output()
		if err != nil {
			return err
		}
		val, err = strconv.ParseFloat(string(out[5:len(out)-3]), 32)
		if err != nil {
			return ErrParsingCpuTemp
		}

	case "X21":
		st, err := cpuStats()
		if err != nil {
			return err
		}
		total1 := st[cpuUser] + st[cpuNice] + st[cpuSystem] + st[cpuIdle] + st[cpuIOWait] +
			st[cpuIRQ] + st[cpuSoftIRQ] + st[cpuSteal] + st[cpuGuest] + st[cpuGuestNice]
		idle1 := st[cpuIdle]

		time.Sleep(1 * time.Second)

		st, err = cpuStats()
		if err != nil {
			return err
		}
		total2 := st[cpuUser] + st[cpuNice] + st[cpuSystem] + st[cpuIdle] + st[cpuIOWait] +
			st[cpuIRQ] + st[cpuSoftIRQ] + st[cpuSteal] + st[cpuGuest] + st[cpuGuestNice]
		idle2 := st[cpuIdle]
		val = (1.0 - (float64(idle2-idle1) / float64(total2-total1))) * 100

	case "X22":
		var ms runtime.MemStats
		runtime.ReadMemStats(&ms)
		val = float64(ms.Sys) / 1024

	default:
		return ErrUnimplemented
	}
	pin.Value = int(val)
	return nil
}

// cpuStats reads CPU stats from /proc/stat
// ToDo: extend for multiple cores
func cpuStats() (stats []int, err error) {
	content, err := ioutil.ReadFile("/proc/stat")
	if err != nil {
		return nil, err
	}

	lines := strings.Split(string(content), "\n")
	line := ""
	for _, ln := range lines {
		if strings.HasPrefix(ln, "cpu") {
			line = ln
			break
		}
	}
	if line == "" {
		return nil, ErrParsingCpuUsage
	}

	values := strings.Fields(line)
	if len(values) != cpuMaxStats {
		return nil, ErrParsingCpuUsage
	}
	stats = make([]int, cpuMaxStats)
	for i, _ := range stats {
		var err error
		stats[i], err = strconv.Atoi(values[i])
		if err != nil {
			stats[i] = 0
		}
	}
	return stats, nil
}
