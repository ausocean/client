//go:build withcv
// +build withcv

/*
DESCRIPTION
  Testing functions for the turbidity sensor using images from
  previous experiment.

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
	"io"
	"testing"

	"github.com/ausocean/utils/logging"
	"gocv.io/x/gocv"
	"gonum.org/v1/gonum/stat"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotutil"
	"gopkg.in/natefinch/lumberjack.v2"
)

const (
	nImages   = 13  // Number of images to test. (Max 13)
	nSamples  = 10  // Number of samples for each image. (Max 10)
	increment = 2.5 // Increment of the turbidity level.
)

// Logging configuration.
const (
	logPath      = "/var/log/netsender/netsender.log"
	logMaxSize   = 500 // MB
	logMaxBackup = 10
	logMaxAge    = 28 // days
	logVerbosity = logging.Info
	logSuppress  = true
)

// TestImages will read a library of test images and calculate the sharpness and contrast scores.
// A plot of the results will be generated and stored in the plots directory.
func TestImages(t *testing.T) {

	const (
		k1, k2       = 4, 4
		filterSize   = 3
		scale, alpha = 1.0, 1.0
	)

	// Create lumberjack logger.
	fileLog := &lumberjack.Logger{
		Filename:   logPath,
		MaxSize:    logMaxSize,
		MaxBackups: logMaxBackup,
		MaxAge:     logMaxAge,
	}
	log := logging.New(logVerbosity, io.MultiWriter(fileLog), logSuppress)

	template := gocv.IMRead("images/template.jpg", gocv.IMReadGrayScale)
	transformMatrix, err := FindTransform("images/default.jpg", "images/template.jpg")
	if err != nil {
		t.Fatalf("could not find transformation: %v", err)
	}
	t.Log(formatMat(transformMatrix))

	imgs := make([][]gocv.Mat, nImages)

	// Load test images.
	for i := range imgs {
		imgs[i] = make([]gocv.Mat, nSamples)
		for j := range imgs[i] {
			imgs[i][j] = gocv.IMRead(fmt.Sprintf("images/t-%v/000%v.jpg", i, j), gocv.IMReadColor)
		}
	}

	ts, err := NewTurbiditySensor(template, transformMatrix, k1, k2, filterSize, scale, alpha, log)
	if err != nil {
		t.Fatalf("could not create turbidity sensor: %v", err)
	}

	results, err := NewResults(nImages)
	if err != nil {
		t.Fatalf("could not create results: %v", err)
	}

	// Score each image by calculating the average score from camera burst.
	for i := range imgs {
		// Evaluate camera burst.
		sample_result, err := ts.Evaluate(imgs[i])
		if err != nil {
			t.Fatalf("evaluation Failed: %v", err)
		}

		// Add the average result from camera burst.
		results.Update(stat.Mean(sample_result.Sharpness, nil), stat.Mean(sample_result.Contrast, nil), float64(i)*increment, i)
	}

	err = plotResults(results.Turbidity, normalize(results.Sharpness), normalize(results.Contrast))
	if err != nil {
		t.Fatalf("plotting Failed: %v", err)
	}

	t.Logf("Sharpness: %v", results.Sharpness)
	t.Logf("Contrast: %v", results.Contrast)
}

// plotResults plots sharpness and contrast scores against the level of almond milk in the container
func plotResults(x, sharpness, contrast []float64) error {
	err := plotToFile(
		"Results",
		"Almond Milk (ml)",
		"Score",
		func(p *plot.Plot) error {
			return plotutil.AddLinePoints(p,
				"Contrast", plotterXY(x, contrast),
				"Sharpness", plotterXY(x, sharpness),
			)
		},
	)
	if err != nil {
		return fmt.Errorf("Could not plot results: %w", err)
	}
	return nil
}

// formatMat creates a formatted transformation matrix string
func formatMat(transformMatrix gocv.Mat) string {
	var out string
	for i := 0; i < transformMatrix.Rows(); i++ {
		for j := 0; j < transformMatrix.Cols(); j++ {
			out += fmt.Sprintf(" %.10f", transformMatrix.GetDoubleAt(i, j))
			if i < 2 || j < 2 {
				out += ","
			}
		}
	}
	return out
}
