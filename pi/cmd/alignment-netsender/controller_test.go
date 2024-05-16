/*
DESCRIPTION
  controller_test.go provides testing of functionality in controller.go.

AUTHORS
  Saxon Nelson-Milton <saxon@ausocean.org>

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
	"math"
	"testing"
)

func TestMedian(t *testing.T) {
	const testN = 5
	med := newRunningMedian(testN)

	tests := []struct {
		update, want float64
	}{
		{13, 13},
		{46, 29.5},
		{5, 13},
		{53, 29.5},
		{26, 26},
		{21, 26},
		{33, 26},
		{67, 33},
		{8, 26},
		{3, 21},
	}

	for i, test := range tests {
		med.update(test.update)
		if med.value != test.want {
			t.Errorf("did not get expected result from test: %d. Got: %f, Want: %f", i, med.value, test.want)
		}
	}
}

func TestStdDev(t *testing.T) {
	const testN = 5
	sd := newRunningStdDev(testN)

	tests := []struct {
		update, want float64
	}{
		{13, 0},
		{46, 23.33},
		{5, 21.73},
		{53, 23.78},
		{26, 20.65},
		{21, 19.41},
		{33, 17.54},
		{67, 19.39},
		{8, 22.10},
		{3, 25.53},
	}

	for i, test := range tests {
		sd.update(test.update)
		got := math.Round(sd.value*100) / 100
		if got != test.want {
			t.Errorf("did not get expected result from test: %d. Got: %f, Want: %f", i, got, test.want)
		}
	}
}
