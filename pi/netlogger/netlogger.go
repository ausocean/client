/*
NAME
  netlogger

DESCRIPTION
  netlogger sends log files using netsender

AUTHOR
  Scott Barnard <scott@ausocean.org>
  Jack Richardson <richardson.jack@outlook.com>

LICENSE
  netlogger is Copyright (C) 2017-2020 the Australian Ocean Lab (AusOcean).

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

// Package netlogger provides a netlogger type which is used for sending log files
// using netsender.
package netlogger

import (
	"bytes"
	"strings"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/utils/sliceutils"
)

// Logger is used for sending log files using netsender. Logger implements io.Writer.
type Logger struct {
	unsent bytes.Buffer
}

// New creates a Logger struct.
func New() *Logger {
	return &Logger{}
}

// Implements io.Writer.
// Write stores log data to be sent in a buffer.
func (l *Logger) Write(p []byte) (n int, err error) {
	return l.unsent.Write(p)
}

// Send unsent logs as JSON text to a NetReceiver service when T0
// is configured as an input, otherwise resets unsent logs.
func (l *Logger) Send(ns *netsender.Sender) error {
	if l.unsent.Len() == 0 {
		return nil // No logs to send.
	}

	ip := strings.Split(ns.Param("ip"), ",")
	if !sliceutils.ContainsString(ip, "T0") {
		l.unsent.Reset()
		return nil // No pin to send them with.
	}

	logs := l.unsent.Bytes()
	pin := netsender.Pin{
		Name:     "T0",
		Value:    len(logs),
		Data:     logs,
		MimeType: "application/json",
	}

	_, _, err := ns.Send(netsender.RequestPoll, []netsender.Pin{pin})
	if err == nil {
		l.unsent.Reset()
	}

	return err
}
