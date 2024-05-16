/*
DESCRIPTION
  calibrate.go provides a calibration results type (Results) for storing
  data points from a "sweep" to collect a mapping of servo angle to, magnetometer
  axis values, and signal strenght. This type provides methods for adding data
  points to the map, fitting parabolas to each set of values (to smooth),
  getting the optimal signal angle and deriving servo angle from a given set
  of mag values.

AUTHORS
  Saxon Nelson-Milton <saxon@ausocean.org>
  Alex Arends <alex@ausocean.org>

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

// Package calibration provides a calibration results type (Results) for storing
// data points from a "sweep" to collect a mapping of servo angle to, magnetometer
// axis values, and signal strenght. This type provides methods for adding data
// points to the map, fitting parabolas to each set of values (to smooth),
// getting the optimal signal angle and deriving servo angle from a given set
// of mag values.
package calibration

import (
	"errors"
	"fmt"
	"math"

	"gonum.org/v1/gonum/mat"
)

// Results holds data points from a sweep.
type Results struct {
	Angles, MagX, MagY, Signal []float64
}

// NewResults returns a new Results with length l.
func NewResults(l int) *Results {
	return &Results{
		Angles: make([]float64, l),
		MagX:   make([]float64, l),
		MagY:   make([]float64, l),
		Signal: make([]float64, l),
	}
}

// add adds a data point to the Results mapping.
func (cr *Results) Add(ang, magX, magY, signal float64) {
	cr.Angles = append(cr.Angles, ang)
	cr.MagX = append(cr.MagX, magX)
	cr.MagY = append(cr.MagY, magY)
	cr.Signal = append(cr.Signal, signal)
}

// fit fits polynomials to the magnetometer axis values and signal strength values
// and returns a new Results with data points corresponding to the applied
// fittings.
func (cr *Results) Fit() (*Results, [3]mat.Matrix, error) {
	newCR := Results{Angles: make([]float64, len(cr.Angles))}
	copy(newCR.Angles, cr.Angles)
	var coeffs [3]mat.Matrix
	var err error

	const polyDegree = 3
	newCR.MagX, coeffs[0], err = fit(cr.Angles, cr.MagX, polyDegree)
	if err != nil {
		return nil, coeffs, fmt.Errorf("could not fit poly to magX data: %w", err)
	}

	newCR.MagY, coeffs[1], err = fit(cr.Angles, cr.MagY, polyDegree)
	if err != nil {
		return nil, coeffs, fmt.Errorf("could not fit poly to magY data: %w", err)
	}

	newCR.Signal, coeffs[2], err = fit(cr.Angles, cr.Signal, polyDegree)
	if err != nil {
		return nil, coeffs, fmt.Errorf("could not fit poly to signal data: %w", err)
	}
	return &newCR, coeffs, nil
}

// bestSignalAngle derives the servo angle for which the best signal corresponds to.
func (cr *Results) BestSignalAngle() (float64, error) {
	max := math.Inf(-1)
	maxIdx := -1
	for i, v := range cr.Signal {
		if v > max {
			max = v
			maxIdx = i
		}
	}
	if maxIdx == -1 || math.IsInf(max, -1) {
		return -1, errors.New("could not find best signal angle")
	}
	return cr.Angles[maxIdx], nil
}

// angleFromMag derives and returns the servo angle that best matches the given
// mag axis values using Euclidean distance between the given mag point and those
// in the Results.
func (cr *Results) AngleFromMag(x, y float64) (float64, error) {
	minDst := math.Inf(1)
	var minDstIdx int
	for i := 0; i < len(cr.MagX); i++ {
		dst := math.Hypot(cr.MagX[i]-x, cr.MagY[i]-y)
		if dst < minDst {
			minDst = dst
			minDstIdx = i
		}
	}
	return cr.Angles[minDstIdx], nil
}
