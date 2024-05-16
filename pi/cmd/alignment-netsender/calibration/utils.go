/*
DESCRIPTION
  utils.go provides useful utilities.

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
	"fmt"

	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/plotutil"
	"gonum.org/v1/plot/vg"
)

const plotFolder = "plots"

// plotSweepResults plots sweep results to a file.
func PlotRawResults(res *Results) error {
	err := plotToFile(
		"Sweep Mag Results",
		"Servo Angle (deg)",
		"Mag Axis Value",
		func(p *plot.Plot) error {
			return plotutil.AddLinePoints(p,
				"x", plotterXY(res.Angles, res.MagX),
				"y", plotterXY(res.Angles, res.MagY),
			)
		},
	)
	if err != nil {
		return fmt.Errorf("could not plot magnetometer sweep results: %v", err)
	}

	err = plotToFile(
		"Sweep Signal Results",
		"Servo Angle (deg)",
		"Signal Strength (dB)",
		func(p *plot.Plot) error {
			return plotutil.AddLinePoints(p,
				"Strength", plotterXY(res.Angles, res.Signal),
			)
		},
	)
	if err != nil {
		return fmt.Errorf("could not plot signal sweep results: %v", err)
	}
	return nil
}

// plotFitResults plots fitting results to a file, superimposing the fitted
// results on top of the raw results.
func PlotFitResults(old, new *Results) error {
	err := plotToFile(
		"Magnetometer Fittings",
		"Servo Angle (deg)",
		"Mag Axis Value",
		func(p *plot.Plot) error {
			return plotutil.AddLinePoints(p,
				"x(raw)", plotterXY(old.Angles, old.MagX),
				"y(raw)", plotterXY(old.Angles, old.MagY),
				"x(fitted)", plotterXY(old.Angles, new.MagX),
				"y(fitted)", plotterXY(old.Angles, new.MagY),
			)
		},
	)
	if err != nil {
		return fmt.Errorf("could not plot magnetometer fitting results: %v", err)
	}

	err = plotToFile(
		"Signal Fitting",
		"Servo Angle (deg)",
		"Signal Strength (dB)",
		func(p *plot.Plot) error {
			return plotutil.AddLinePoints(p,
				"signal(raw)", plotterXY(old.Angles, old.Signal),
				"signal(fitted)", plotterXY(old.Angles, new.Signal),
			)
		},
	)
	if err != nil {
		return fmt.Errorf("could not plot signal strength fitting results: %v", err)
	}
	return nil
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

	if err := p.Save(15*vg.Centimeter, 15*vg.Centimeter, plotFolder+"/"+name+".png"); err != nil {
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
