/*
DESCRIPTION
  fit.go provides functions for fitting a polynomial to a dataset.

AUTHORS
  Alex Arends <alex@ausocean.org>
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
	"fmt"
	"math"

	"gonum.org/v1/gonum/mat"
)

// fit fits a polynomial of degree to the data provided in x and y. The y values
// corresponding to the fit, along with the coefficients of the matched
// polynomial are returned.
func fit(x, y []float64, degree int) ([]float64, mat.Matrix, error) {
	a := vandermonde(x, degree)
	b := mat.NewVecDense(len(y), y)
	c := mat.NewVecDense(degree+1, nil)

	qr := new(mat.QR)
	qr.Factorize(a)

	err := qr.SolveVecTo(c, false, b)
	if err != nil {
		return nil, nil, fmt.Errorf("could not solve QR: %v", err)
	}

	fitted := make([]float64, len(x))
	for i, v := range x {
		for j := degree; j >= 0; j-- {
			fitted[i] += c.AtVec(j) * math.Pow(v, float64(j))
		}
	}
	return fitted, c, nil
}

// vandermode calculates the vandermode matrix for set a and the given degree.
func vandermonde(a []float64, degree int) *mat.Dense {
	x := mat.NewDense(len(a), degree+1, nil)
	for i := range a {
		for j, p := 0, 1.0; j <= degree; j, p = j+1, p*a[i] {
			x.Set(i, j, p)
		}
	}
	return x
}
