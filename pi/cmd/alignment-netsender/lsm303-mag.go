/*
DESCRIPTION
  compass.go provides an implementation of the Compass interface for the
  LSM303 magnetometer/accelerometer module.

AUTHORS
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
	"os/exec"
	"strconv"
	"strings"
	"sync"

	"github.com/ausocean/utils/logging"
)

// LSM303Magnetometer is an implementation of the Magnetometer interface for the
// LSM303 Accel/Mag module that uses a child process responsible for I2C
// communication to obtain magnetometer axis values.
//
// TODO: remove reliance on child python process to get mag values. Implement
// I2C comms with LSM303 module.
type LSM303Magnetometer struct {
	mu      sync.Mutex
	x, y, z float64
	err     error // Holds any errors that may occur.

	in      *bufio.Scanner // Scans stdout of the mag python process for headings.
	cmd     *exec.Cmd      // Holds the magnetometer python process.
	done    chan struct{}  // To signal finishing of mag axis value reading.
	log     logging.Logger
	outPipe io.ReadCloser
}

// NewLSM303Magnetometer returns a new LSM303Magnetometer. A background python script
// responsible for dealing with the LSM303 interfacing is started and as well as
// a routine responsible for the reading from this process.
func NewLSM303Magnetometer(l logging.Logger) (*LSM303Magnetometer, error) {
	c := &LSM303Magnetometer{
		log:  l,
		done: make(chan struct{}),
	}

	var err error
	c.cmd = exec.Command(python, "-c", magScript)
	c.outPipe, err = c.cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("could not pipe stdout: %w", err)
	}

	// TODO: check stderr.

	c.in = bufio.NewScanner(c.outPipe)

	c.log.Debug("starting magnetometer script")
	err = c.cmd.Start()
	if err != nil {
		return nil, fmt.Errorf("could not start magnetometer command: %w", err)
	}

	go c.readAxes()

	return c, nil
}

// readAxes is a routine responsible for reading magnetometer axes values from
// the background mag python process. Values are stored in LSM303Magnetometer
// axis fields using a concurrency safe method.
func (c *LSM303Magnetometer) readAxes() {
	for c.in.Scan() {
		select {
		case <-c.done:
			return
		default:
		}

		var vals [3]float64
		var err error

		strs := strings.Split(c.in.Text(), ",")
		if len(strs) != 3 {
			c.setErr(fmt.Errorf("did not get expected CSV format, got: %v", c.in.Text()))
			return
		}

		for i := range vals {
			vals[i], err = strconv.ParseFloat(strs[i], 64)
			if err != nil {
				c.setErr(fmt.Errorf("could not parse mag value: %d: %w", i, err))
			}
		}

		c.setValues(vals[0], vals[1], vals[2])
	}

	err := c.in.Err()
	if err == nil {
		err = io.EOF
		c.setErr(err)
		return
	}
	c.setErr(fmt.Errorf("could not scan mag axes values: %w", err))
}

// setErr is a concurrency safe method used by the readAxes method to set the
// latest error state (if any) for return by the Err method.
func (c *LSM303Magnetometer) setErr(e error) {
	c.mu.Lock()
	c.err = e
	c.mu.Unlock()
}

// setValues is a concurrency safe method used by readAxes to set latest mag axis
// values.
func (m *LSM303Magnetometer) setValues(x, y, z float64) {
	m.mu.Lock()
	m.x, m.y, m.z = x, y, z
	m.mu.Unlock()
}

// Values is a concurrency safe method for retrieving the most recent magnetometer
// axis values.
func (m *LSM303Magnetometer) Values() (x, y, z float64, err error) {
	m.mu.Lock()
	x, y, z = m.x, m.y, m.z
	err = m.err
	m.mu.Unlock()
	return
}

// Shutdown sends a termination signal to the readAxes routine, closes the stdout
// pipe and kills the background python process.
func (c *LSM303Magnetometer) Shutdown() error {
	if c.done != nil {
		close(c.done)
	}

	err := c.outPipe.Close()
	if err != nil {
		return fmt.Errorf("could not close stdout pipe: %w", err)
	}

	err = c.cmd.Process.Kill()
	if err != nil {
		return fmt.Errorf("could not kill heading process: %w", err)
	}
	return nil
}

// Python script used to get magnetometer readings from LSM303 module.
const magScript = `
import math
import logging
import sys
import threading
import datetime
import time
import random
import busio
import board
import adafruit_lsm303dlh_mag

# Logging settings.
LOG_TO_FILE = False
LOG_FILE = "heading.log"
LOG_LEVEL = logging.DEBUG

# Delay after sample retrieval (consistent with ODR of 7.5Hz for LSM303 mag.)
SAMPLE_DELAY = 0.14

# Pseudo magnetometer constants.
THREAD_DELAY = 0.01
RAND_DIFF = 2

# Set up logging.
if LOG_TO_FILE:
	logging.basicConfig(filename=LOG_FILE,level=LOG_LEVEL)
else:
	logging.basicConfig(stream=sys.stdout,level=LOG_LEVEL)

# init initialised the accel/mag module and servo.
def init():
	i2c = busio.I2C(board.SCL, board.SDA)
	mag = adafruit_lsm303dlh_mag.LSM303DLH_Mag(i2c)
	return mag

# getMagReadings obtains mag axis values from the magnetometer implementation and
# returns normalised values, only as fast as the ODR of the LSM303 magnetometer.
def getMagReadings(mag):
	time.sleep(SAMPLE_DELAY)
	x,y,z = mag.magnetic
	norm = math.sqrt(x*x+y*y+z*z)
	return x/norm,y/norm,z/norm


# main attempts to align a servo based on accel/mag heading.
def main():
	while True:
			try:
					mag = init()
			except IOError as e:
					logging.debug(e)
					continue
			except:
					logging.debug("initialisation error")
					continue

			try:
					while True:
							x,y,z=getMagReadings(mag)
							print(','.join([str(x),str(y),str(z)]))
							sys.stdout.flush()
			except IOError as e:
					logging.debug(e)
					continue
			finally:
					logging.debug("unknown failure")

if __name__ == "__main__":
	main()
`
