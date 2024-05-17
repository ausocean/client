/*
DESCRIPTION
  Plotting functions for the turbidity sensor results.

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

import (
	"fmt"
	"math"

	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/vg"
)

// normalise normalises the values in the given slice to the range [0,1] inclusive.
func normalize(s []float64) []float64 {
	max := -math.MaxFloat64
	min := math.MaxFloat64
	out := make([]float64, len(s))

	if len(s) <= 1 {
		return s
	}

	// Find the max and min values of the s.
	for i := range s {
		if s[i] > max {
			max = s[i]
		}
		if s[i] < min {
			min = s[i]
		}
	}

	for i := range s {
		out[i] = (s[i] - min) / (max - min)
	}
	return out
}

// plotToFile creates a plot with a specified name and x&y titles using the
// provided draw function, and then saves to a PNG file with filename of name.
func plotToFile(name, xTitle, yTitle string, draw func(*plot.Plot) error) error {
	p := plot.New()
	p.Title.Text = name
	p.X.Label.Text = xTitle
	p.Y.Label.Text = yTitle
	err := draw(p)
	if err != nil {
		return fmt.Errorf("could not draw plot contents: %w", err)
	}
	if err := p.Save(15*vg.Centimeter, 15*vg.Centimeter, "plots/"+name+".png"); err != nil {
		return fmt.Errorf("could not save plot: %w", err)
	}
	return nil
}

// plotterXY provides a plotter.XYs type value based on the given x and y data.
func plotterXY(x, y []float64) plotter.XYs {
	xy := make(plotter.XYs, len(x))
	for i := range x {
		xy[i].X = x[i]
		xy[i].Y = y[i]
	}
	return xy
}
