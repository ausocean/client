/*
NAME
	smartlogger - smartlogger implements log file rotation and can send old files to netreceiver for storage

DESCRIPTION
  See Readme.md

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>


LICENSE
  smartlogger is Copyright (C) 2017-2018 the Australian Ocean Lab (AusOcean).

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

package smartlogger

import (
	"io/ioutil"
	"os"
	"path/filepath"

	"gopkg.in/natefinch/lumberjack.v2"

	"github.com/ausocean/client/pi/netsender"
)

const mimeType = "text/plain" // mime-type to send to NetReceiver

type Smartlogger struct {
	path      string
	LogRoller lumberjack.Logger
	keepLogs  bool
}

// Rotate closes the current log file and dates it, followed by opening a new log file
func (s *Smartlogger) Rotate() error {
	return s.LogRoller.Rotate()
}

// New generates and returns a new logger object
func New(path string) *Smartlogger {
	return &Smartlogger{
		path: path,
		LogRoller: lumberjack.Logger{
			Filename:   filepath.Join(path, "netsender.log"),
			MaxSize:    500, // megabytes
			MaxBackups: 10,
			MaxAge:     28, // days
		},
	}
}

//SetKeepLogs sets whether the logger should keep logs on disk after seding them to the cloud
func (s *Smartlogger) SetKeepLogs(kl bool) {
	s.keepLogs = kl
}

// SendLogs uses netsender to send all backup log files as text to netreciever. On a succesful send, the file is deleted.
// On failure, the log file is retained and will be sent at next call of SendLogs.
// A call to SendLogs should be preceded by a call to Rotate if most recent log messages are required to be sent.
func (s *Smartlogger) SendLogs(ns *netsender.Sender) {
	var err error
	var logFiles []string

	logFiles, err = filepath.Glob(filepath.Join(s.path, "netsender-*"))
	if err != nil {
		s.LogRoller.Write([]byte("Can't glob matching log files\n"))
	}

	pins := netsender.MakePins(ns.Param("ip"), "T")

	for _, ff := range logFiles {
		// ff is an full file name; we need the local (base) file name too
		lf := filepath.Base(ff)
		logFileContent, err := ioutil.ReadFile(ff)

		if err != nil {
			s.LogRoller.Write([]byte("Can't read log file" + lf + "\n"))
		}

		for i := range pins {
			if pins[i].Name == "T0" {
				pins[i].Value = len(logFileContent)
				pins[i].Data = logFileContent
				pins[i].MimeType = mimeType
			}
		}
		_, _, err = ns.Send(netsender.RequestPoll, pins)

		if err != nil {
			s.LogRoller.Write([]byte("Can't send Log File contents for " + lf + ". Received error: " + err.Error() + "\n"))
			continue
		}
		if !s.keepLogs {
			if err = os.Remove(ff); err != nil {
				s.LogRoller.Write([]byte("Can't delete logFile" + lf + ". Err: " + err.Error() + "\n"))
				//TODO: should we try redelete / signal this is a duplicate now or leave to cloud to detect duplicates??
			}
		} else {
			if _, err := os.Stat(filepath.Join(s.path, "backups")); os.IsNotExist(err) {
				os.Mkdir(filepath.Join(s.path, "backups"), os.ModePerm)
			}
			if err = os.Rename(ff, filepath.Join(s.path, "backups", lf)); err != nil {
				s.LogRoller.Write([]byte("Can't move logFile" + lf + ". Err: " + err.Error() + "\n"))
				//TODO: should we try redelete / signal this is a duplicate now or leave to cloud to detect duplicates??
			}
		}

	}
}
