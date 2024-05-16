/*
DESCRIPTION
  calibrate_test.go provides testing for functionality in calibrate.go.

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

package calibration

import (
	"os"
	"testing"

	"gonum.org/v1/gonum/mat"
)

// TestFit tests that the CalibResults fit method correctly fits polynomials to
// the data.
func TestFit(t *testing.T) {
	cr := &Results{
		Angles: anglesSample,
		MagX:   magXSample,
		MagY:   magYSample,
		Signal: signalSample,
	}

	expected := [3][4]float64{
		{-0.6589032280402789, -0.08422737031500092, 0.001392198221130234, -4.815550114300251e-06},
		{-5.860515057910634, 0.18395496415653748, -0.00031294746329997184, -3.7104569221468917e-06},
		{-56.66196309027109, 1.965939048133087, -0.03034473885675087, 0.00010814619024018537},
	}

	_, coeffs, err := cr.Fit()
	if err != nil {
		t.Errorf("could not fit data: %v", err)
	}

	for i, vals := range expected {
		vec := mat.NewVecDense(4, vals[:])
		if !mat.Equal(vec, coeffs[i]) {
			t.Errorf("did not get expected result for coeffs: %d", i)
		}
	}
}

// TestBestSignalAngle checks that we can perform a fit and then derive the best
// signal angle from the fitted signal data.
func TestBestSignalAngle(t *testing.T) {
	c := &Results{
		Angles: anglesSample,
		MagX:   magXSample,
		MagY:   magYSample,
		Signal: signalSample,
	}

	const expected = 47.0

	_, _, err := c.Fit()
	if err != nil {
		t.Fatalf("could not fit data: %v", err)
	}

	angle, err := c.BestSignalAngle()
	if err != nil {
		t.Errorf("could not find best signal angle: %v", err)
	}

	if angle != expected {
		t.Errorf("did not get expected signal angle. Got: %f, Want: %f", angle, expected)
	}
}

// TestAngleFromMag checks that we can derive a corresponding servo angle for
// the given x and y magnetomer values.
func TestAngleFromMag(t *testing.T) {
	c := &Results{
		Angles: anglesSample,
		MagX:   magXSample,
		MagY:   magYSample,
		Signal: signalSample,
	}

	_, _, err := c.Fit()
	if err != nil {
		t.Fatalf("could not fit data: %v", err)
	}

	tests := []struct {
		x, y float64
		want float64
	}{
		{x: -1.5, y: -3.5, want: 16},
		{x: -1.5, y: 4, want: 60},
		{x: 1, y: 5, want: 115},
		{x: -1, y: -7, want: 0},
		{x: 1, y: -4, want: 178},
	}

	for i, test := range tests {
		angle, err := c.AngleFromMag(test.x, test.y)
		if err != nil {
			t.Errorf("could not get angle form magnetometer points for test: %d: %v", i, err)
			continue
		}

		if angle != test.want {
			t.Errorf("did not get expected angle for mag readings: %d. Want: %f, Got: %f", i, test.want, angle)
		}
	}
}

// TestPlot checks that our plotting functions correclty plot and save to file.
func TestPlot(t *testing.T) {
	_, err := os.Stat(plotFolder)
	if os.IsNotExist(err) {
		t.Skip("plot folder does not exist, skipping")
	}

	c := &Results{
		Angles: anglesSample,
		MagX:   magXSample,
		MagY:   magYSample,
		Signal: signalSample,
	}

	err = PlotRawResults(c)
	if err != nil {
		t.Errorf("could not plot raw calibration data: %v", err)
	}

	cFit, _, err := c.Fit()
	if err != nil {
		t.Fatalf("could not fit data: %v", err)
	}

	err = PlotFitResults(c, cFit)
	if err != nil {
		t.Errorf("could not plot fitted results: %v", err)
	}
}
