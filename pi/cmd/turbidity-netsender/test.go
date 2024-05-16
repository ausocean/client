// +build test

/*
DESCRIPTION
  test.go modifies build functionality when 'test' tag is used.

AUTHORS
  Harrison Telford <harrison@ausocean.org>

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
  along with revid in gpl.txt. If not, see http://www.gnu.org/licenses.
*/

package main

import (
	"testing"
	"time"

	"github.com/ausocean/utils/logging"
)

const nReads = 50 // Number of values to read.
const delay = 100 * time.Millisecond

// RaspiSerialPacket is used to test the fucntionality of turbidity.go's
// ability to read serial output from the Raspberry Pi through pythonPath.
func RaspiSerialPacket(t *testing.T) {
	ts, err := newTurb(pythonPath, (*logging.TestLogger)(t))
	if err != nil {
		t.Fatalf("did not expect error on sensor initialisation: %v", err)
	}

	// Reads some values.
	for i := 0; i < nReads; i++ {
		value := ts.Turbidity()
		e := ts.Err()
		if e != nil {
			t.Fatalf("did not expect error: %v", e)
		}
		t.Logf("turbidity reading: %v", value)
		time.Sleep(delay)
	}
}

// DummyValuePacket is used to test the functionality of turbidity.go's
// ability to parse the output from the Remond Turbidity Sensor through
// the file dummy_turbidity.py.
func DummyValuePacket(t *testing.T) {
	ts, err := newTurb(dummyPath, (*logging.TestLogger)(t))
	if err != nil {
		t.Fatalf("did not expect error on sensor initialisation: %v", err)
	}

	// Reads some values.
	for i := 0; i < nReads; i++ {
		value := ts.Turbidity()
		e := ts.Err()
		if e != nil {
			t.Fatalf("did not expect error: %v", e)
		}
		t.Logf("turbidity reading: %v", value)
		time.Sleep(delay)
	}
}
