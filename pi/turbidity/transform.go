//go:build withcv
// +build withcv

/*
DESCRIPTION
  Provides a function which can extract the transformation matrix.

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
	"image"

	"gocv.io/x/gocv"
)

// Perspective transformation constants.
const (
	ransacThreshold = 3.0   // Maximum allowed reprojection error to treat a point pair as an inlier.
	maxIter         = 2000  // The maximum number of RANSAC iterations.
	confidence      = 0.995 // Confidence level, between 0 and 1.
)

// FindTransform, given a template and standard image the perspetive transformation matrix will be determined.
// the matrix will be returned and logged for use in vidgrind.
func FindTransform(standardPath, templatePath string) (gocv.Mat, error) {
	mask := gocv.NewMat()
	std := gocv.IMRead(standardPath, gocv.IMReadColor)
	stdCorners := gocv.NewMat()

	template := gocv.IMRead(templatePath, gocv.IMReadGrayScale)
	templateCorners := gocv.NewMat()
	transformMatrix := gocv.NewMat()

	// Validate template image is not empty and has valid corners.
	if template.Empty() {
		return transformMatrix, errors.New("template image is empty")
	}
	if !gocv.FindChessboardCorners(template, image.Pt(3, 3), &templateCorners, gocv.CalibCBNormalizeImage) {
		return transformMatrix, errors.New("could not find corners in template image")
	}

	// Validate standard image is not empty and has valid corners.
	if std.Empty() {
		return transformMatrix, errors.New("standard image is empty")
	}
	if !gocv.FindChessboardCorners(std, image.Pt(3, 3), &stdCorners, gocv.CalibCBNormalizeImage) {
		return transformMatrix, errors.New("could not find corners in standard image")
	}

	transformMatrix = gocv.FindHomography(stdCorners, &templateCorners, gocv.HomograpyMethodRANSAC, ransacThreshold, &mask, maxIter, confidence)
	return transformMatrix, nil
}
