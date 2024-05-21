/*
NAME
  testlogger -  a simple logger that just prints strings using fmt.Print.

AUTHOR
  Alan Noble <alan@ausocean.org>

LICENSE
  Copyright (C) 2018 the Australian Ocean Lab (AusOcean).

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
	"fmt"
	"os"

	"github.com/ausocean/client/pi/netsender"
)

// TestLogger implements a netsender.Logger that just writes to stdout.
type TestLogger struct {
	level int8
}

// SetLevel sets the level of the Logger. Calls to Log with a
// level above the set level will be logged, all others will
// be omitted from logging.
func (tl *TestLogger) SetLevel(level int8) {
	tl.level = level
}

// Log requests the Logger to write a message at the given level, with
// optional key/values pairs. Key are strings and values must be
// either a string or map[string]string)
func (tl *TestLogger) Log(level int8, message string, params ...interface{}) {
	if tl.level > level {
		return
	}

	exit := false
	switch level {
	case netsender.DebugLevel:
		fmt.Print("Debug: " + message)
	case netsender.InfoLevel:
		fmt.Print("Info: " + message)
	case netsender.WarningLevel:
		fmt.Print("Warning: " + message)
	case netsender.ErrorLevel:
		fmt.Print("Error: " + message)
	case netsender.FatalLevel:
		fmt.Print("Fatal: " + message)
		exit = true
	default:
		fmt.Print("Fatal (unimplemented): " + message)
		exit = true
	}

	for i, param := range params {
		if i%2 == 0 {
			fmt.Print(" " + param.(string))
		} else {
			s, ok := param.(string)
			if ok {
				fmt.Print(":" + s)
				continue
			}
			m, ok := param.(map[string]string)
			if ok {
				fmt.Printf(":%v", m)
			}
		}
	}
	fmt.Println()

	if exit {
		os.Exit(1)
	}
}
