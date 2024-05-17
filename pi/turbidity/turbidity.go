//go:build withcv
// +build withcv

/*
DESCRIPTION
  Holds the turbidity sensor struct. Can evaluate 4x4 chessboard markers in an
  image to measure the sharpness and contrast. This implementation is based off
  a master thesis from Aalborg University, Turbidity measurement based on computer vision.
  The full paper is avaible at https://projekter.aau.dk/projekter/files/306657262/master.pdf

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
	"errors"
	"fmt"
	"image"
	"math"
	"time"

	"github.com/ausocean/utils/logging"
	"gocv.io/x/gocv"
)

// TurbiditySensor is a software based turbidity sensor that uses CV to determine sharpness and constrast level
// of a chessboard-like target submerged in water that can be correlated to turbidity/visibility values.
type TurbiditySensor struct {
	template                gocv.Mat // Holds the image of the target.
	TransformMatrix         gocv.Mat // The current perspective transformation matrix to extract the target from the frame.
	k1, k2, sobelFilterSize int
	scale, alpha            float64
	log                     logging.Logger
}

// NewTurbiditySensor returns a new TurbiditySensor.
func NewTurbiditySensor(template, transformMatrix gocv.Mat, k1, k2, sobelFilterSize int, scale, alpha float64, log logging.Logger) (*TurbiditySensor, error) {
	ts := new(TurbiditySensor)

	// Validate template image is not empty and has valid corners.
	if template.Empty() {
		return nil, errors.New("template image is empty")
	}

	ts.template = template
	ts.TransformMatrix = transformMatrix
	ts.k1, ts.k2, ts.sobelFilterSize = k1, k2, sobelFilterSize
	ts.alpha, ts.scale = alpha, scale
	ts.log = log
	return ts, nil
}

// Evaluate, given a slice of images, return the sharpness and contrast scores.
func (ts TurbiditySensor) Evaluate(imgs []gocv.Mat) (*Results, error) {
	result, err := NewResults(len(imgs))
	if err != nil {
		return nil, fmt.Errorf("could not create results: %w", err)
	}

	for i := range imgs {
		timer := time.Now()
		marker, err := ts.transform(imgs[i])
		if err != nil {
			return nil, fmt.Errorf("could not transform image: %d: %w", i, err)
		}

		ts.log.Debug("transform successful", "transform duration (sec)", time.Since(timer).Seconds())

		timer = time.Now()
		edge := ts.sobel(marker)
		ts.log.Debug("sobel filter successful", "sobel duration", time.Since(timer).Seconds())

		timer = time.Now()
		sharpScore, contScore, err := ts.EvaluateImage(marker, edge)
		if err != nil {
			return result, err
		}
		ts.log.Debug("sharpness and contrast evaluation successful", "evaluation duration", time.Since(timer).Seconds())
		result.Update(sharpScore, contScore, float64(i), i)
	}
	return result, nil
}

// EvaluateImage will evaluate image sharpness and contrast using blocks of size k1 by k2. Return the respective scores.
func (ts TurbiditySensor) EvaluateImage(img, edge gocv.Mat) (float64, float64, error) {
	var sharpness float64
	var contrast float64

	if img.Rows()%ts.k1 != 0 || img.Cols()%ts.k2 != 0 {
		return math.NaN(), math.NaN(), fmt.Errorf("dimensions not compatible (%v, %v)", ts.k1, ts.k2)
	}
	lStep := img.Rows() / ts.k1
	kStep := img.Cols() / ts.k2

	for l := 0; l < img.Rows(); l += lStep {
		for k := 0; k < img.Cols(); k += kStep {
			// Enhancement Measure Estimation (EME), provides a measure of the sharpness.
			sharpness += ts.evaluateBlockEME(edge, l, k, l+lStep, k+kStep)

			// AMEE, provides a measure of the contrast.
			contrast += ts.evaluateBlockAMEE(img, l, k, l+lStep, k+kStep)
		}
	}

	// Scale EME based on block size.
	sharpness = 2.0 / (float64(ts.k1 * ts.k2)) * sharpness

	// Scale and flip AMEE based on block size.
	contrast = -1.0 / (float64(ts.k1 * ts.k2)) * contrast

	return sharpness, contrast, nil
}

// minMax returns the max and min pixel values of an image block.
func (ts TurbiditySensor) minMax(img gocv.Mat, xStart, yStart, xEnd, yEnd int) (float64, float64) {
	max := -math.MaxFloat64
	min := math.MaxFloat64

	for i := xStart; i < xEnd; i++ {
		for j := yStart; j < yEnd; j++ {
			value := float64(img.GetUCharAt(i, j))

			// Check max/min conditions, zero values are ignoredt to avoid divison by 0.
			if value > max && value != 0.0 {
				max = value
			}
			if value < min && value != 0.0 {
				min = value
			}
		}
	}
	return max, min
}

// evaluateBlockEME will evaluate an image block and return the value to be added to the sharpness result.
func (ts TurbiditySensor) evaluateBlockEME(img gocv.Mat, xStart, yStart, xEnd, yEnd int) float64 {
	max, min := ts.minMax(img, xStart, yStart, xEnd, yEnd)

	// Blocks where all pixel values are equal are ignored to avoid division by 0.
	if max != -math.MaxFloat64 && min != math.MaxFloat64 && max != min {
		return math.Log(max / min)
	}
	return 0.0
}

// evaluateBlockAMEE will evaluate an image block and return the value to be added to the contrast result.
func (ts TurbiditySensor) evaluateBlockAMEE(img gocv.Mat, xStart, yStart, xEnd, yEnd int) float64 {
	max, min := ts.minMax(img, xStart, yStart, xEnd, yEnd)

	// Blocks where all pixel values are equal are ignored to avoid division by 0.
	if max != -math.MaxFloat64 && min != math.MaxFloat64 && max != min {
		contrast := (max + min) / (max - min)
		return math.Pow(ts.alpha*(contrast), ts.alpha) * math.Log(contrast)
	}
	return 0.0
}

// transform will search img for matching template. Returns the transformed image which best match the template.
func (ts TurbiditySensor) transform(img gocv.Mat) (gocv.Mat, error) {
	out := gocv.NewMat()

	if img.Empty() {
		return out, errors.New("image is empty, cannot transform")
	}
	// Check image for corners, if non can be found corners will be set to default value.
	// if !gocv.FindChessboardCorners(img, image.Pt(3, 3), &imgCorners, gocv.CalibCBFastCheck) {}

	// Find and apply transformation.
	gocv.WarpPerspective(img, &out, ts.TransformMatrix, image.Pt(ts.template.Rows(), ts.template.Cols()))
	gocv.CvtColor(out, &out, gocv.ColorRGBToGray)
	return out, nil
}

// sobel will apply sobel filter to an image and return the result.
func (ts TurbiditySensor) sobel(img gocv.Mat) gocv.Mat {
	dx := gocv.NewMat()
	dy := gocv.NewMat()
	sobel := gocv.NewMat()

	// Apply filter.
	gocv.Sobel(img, &dx, gocv.MatTypeCV64F, 0, 1, ts.sobelFilterSize, ts.scale, 0.0, gocv.BorderConstant)
	gocv.Sobel(img, &dy, gocv.MatTypeCV64F, 1, 0, ts.sobelFilterSize, ts.scale, 0.0, gocv.BorderConstant)

	// Convert to unsigned.
	gocv.ConvertScaleAbs(dx, &dx, 1.0, 0.0)
	gocv.ConvertScaleAbs(dy, &dy, 1.0, 0.0)

	// Add x and y components.
	gocv.AddWeighted(dx, 0.5, dy, 0.5, 0, &sobel)

	return sobel
}
