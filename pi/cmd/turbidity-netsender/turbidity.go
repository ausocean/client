/*
DESCRIPTION
  turbidity.go provides functionality for the reading of the Remond Turbidity
  Sensor.

AUTHORS
  Harrison Telford <harrison@ausocean.org>
  Saxon Nelson-Milton <saxon@ausocean.org>

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

package main

import (
	"bufio"
	"fmt"
	"io"
	"io/ioutil"
	"os/exec"
	"strconv"
	"sync"
	"time"

	"github.com/ausocean/utils/logging"
)

// Consts for python script responsible for getting Turbidity values from
// Arduino Uno via Raspberry Pi USB port (ttylACM0).
// dummyPath produces testing values.
const (
	pythonCommand = "python3"
	pythonPath    = "./raspiSerial.py"
	dummyPath     = "./dummy_turbidity.py"
)

// Delays.
const (
	readDelay = 500 * time.Millisecond
	startWait = 5 * time.Second
)

// TurbiditySensor will give functionality to the Remond Turbidity Sensor
// using a background process in which an Arduino Uno sends turbidity readings
// to a Raspberry Pi via USB Serial.
type TurbiditySensor struct {
	turbidity int
	_err      error
	in        *bufio.Scanner // Scans stdout of the turbidity python process for readings.
	cmd       *exec.Cmd      // Hold background process for communicating with arduino uno.
	mu        sync.Mutex     // Need to synchronise access to up to date stored turbidity values.
	done      chan struct{}  // To signal finishing of turbidity sensor reading.
	log       logging.Logger
}

// NewTurbiditySensor returns a new TurbiditySensor based on an underlying python
// process responsible for integrating with turbidity sensing hardware and providing
// values. NewTurbiditySensor wraps new with which different processes may be
// specified i.e. for testing.
func NewTurbiditySensor(l logging.Logger) (*TurbiditySensor, error) {
	ts, err := newTurb(pythonPath, l)
	if err != nil {
		return nil, fmt.Errorf("could not create new TurbiditySensor based on %s: %w", pythonPath, err)
	}
	return ts, nil
}

func newTurb(path string, l logging.Logger) (*TurbiditySensor, error) {
	ts := &TurbiditySensor{
		turbidity: -1,
		log:       l,
		done:      make(chan struct{}),
	}

	ts.cmd = exec.Command(pythonCommand, path)
	stdout, err := ts.cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("could not pipe stdout: %w", err)
	}

	stderr, err := ts.cmd.StderrPipe()
	if err != nil {
		return nil, fmt.Errorf("could not pipe stderr: %w", err)
	}

	go func() {
		errs, err := ioutil.ReadAll(stderr)
		if err != nil && err != io.EOF {
			panic("could not read stderr")
		}
		ts.log.Debug("error on stderr", "error", string(errs))
	}()

	ts.in = bufio.NewScanner(stdout)

	ts.log.Debug("starting turbidity script")
	err = ts.cmd.Start()
	if err != nil {
		return nil, fmt.Errorf("could not start turbidity command: %w", err)
	}

	time.Sleep(startWait)

	go ts.readValue()

	return ts, nil
}

// Routine for reading turbidity values from background python process.
func (ts *TurbiditySensor) readValue() {
	for {
		time.Sleep(readDelay)
		select {
		case <-ts.done:
			return
		default:
		}

		ts.log.Debug("scanning turbidity value")
		if !ts.in.Scan() {
			ts.setTurbidity(-1)
			err := ts.in.Err()
			if err == nil {
				err = io.EOF

			}
			ts.setErr(fmt.Errorf("could not scan next value: %w", err))
			ts.log.Debug("scan error", "error", err)
		}

		// Loop will read escape characters output by the Remond turbidity sensor,
		// then continue to read the turbidity value and set as t.
		pf, err := strconv.ParseFloat(ts.in.Text(), 64)
		if err != nil {
			ts.log.Debug("string to int conversion error,skipping", "error", err)
			continue
		}
		t := int(pf)
		ts.setTurbidity(t)
		ts.log.Debug("got turbidity value", "turbidity", t)
	}
}

// Turbidity() returns the most up to date turbidity reading. Concurrency safe.
func (ts *TurbiditySensor) Turbidity() int {
	ts.mu.Lock()
	t := ts.turbidity
	ts.mu.Unlock()
	return t
}

// setTurbdity() is used by the readTurbidity routine to safely set the turbidity value
// for return.
func (ts *TurbiditySensor) setTurbidity(t int) {
	ts.mu.Lock()
	ts.turbidity = t
	ts.mu.Unlock()
}

// Err returns the most recently applicable error (nil if none applicable).
// Concurrency safe.
func (ts *TurbiditySensor) Err() error {
	ts.mu.Lock()
	err := ts._err
	ts.mu.Unlock()
	return err
}

// setErr is used by the TurbiditySensor to set any errors for return by the Err
// method.
func (ts *TurbiditySensor) setErr(e error) {
	ts.mu.Lock()
	ts._err = e
	ts.mu.Unlock()
}
