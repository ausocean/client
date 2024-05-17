/*
DESCRIPTION
  Results struct used to store results from the turbidity sensor.

AUTHORS
  Russell Stanley <russell@ausocean.org>

LICENSE
  Copyright (C) 2021-2022 the Australian Ocean Lab (AusOcean)

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  in gpl.txt.  If not, see http://www.gnu.org/licenses.
*/

package turbidity

import "fmt"

// Results holds the results of the turbidity sensor.
type Results struct {
	Turbidity []float64
	Sharpness []float64
	Contrast  []float64
}

// NewResults returns a new Results.
func NewResults(n int) (*Results, error) {
	if n <= 0 {
		return nil, fmt.Errorf("invalid result size: %v", n)
	}

	r := new(Results)
	r.Turbidity = make([]float64, n)
	r.Sharpness = make([]float64, n)
	r.Contrast = make([]float64, n)

	return r, nil
}

// Update adds new values to slice at specified index.
func (r *Results) Update(newSat, newCont, newTurb float64, index int) {
	r.Sharpness[index] = newSat
	r.Contrast[index] = newCont
	r.Turbidity[index] = newTurb
}
