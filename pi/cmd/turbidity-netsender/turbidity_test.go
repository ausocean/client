/*
DESCRIPTION
  turbidity_test.go tests functionality of the Remond Turbidity Sensor
  through two path options. TestDummyTurbidity will test the parse
  functionality, reading only the interger values produced by the Sensor
  through a dummy script. TestTurbidity will test the ability to
  read the serial output of the Raspberry Pi through the pythonPath
  command.

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
	"testing"
)

// TestTurbidity calls NewTurbiditySensor to read test turbidity values from
// Remond Turbidity sensor with the pythonPath command.
func TestTurbidity(t *testing.T) {
	RaspiSerialPacket()
}

// TestDummyTurbidity calls NewTurbiditySensor to read dummy turbidity values
// from dummy_turbidity.py with the dummyPath command.
func TestDummyTurbidity(t *testing.T) {
	DummyValuePacket()
}
