//go:build nocpe
// +build nocpe

/*
DESCRIPTION
  nocpe.go provides an pseudo link that satisfies the Link interface for use
  when the the hardware is not available. The provided implementation for the
  most part does little to replicate characteristics of a net link, besides
  the Signal method, which can be used to simulate changing signal strength.

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
	"time"
)

// newLink returns a Link with an underlying pseudoLink type value.
// Function arguments are neglected.
// To see release version of this function, consult cpe.go.
func newLink(device, ip, port, user, pass string) (Link, error) {
	n := time.Now()
	return &pseudoLink{start: n}, nil
}

// pseudoLink implements the Link interface and provides pseudo link statistics.
type pseudoLink struct {
	start time.Time // Used to simulate changing signal strength over time.
}

// Update is only here to satisfy the Link interface; no behaviour.
func (l *pseudoLink) Update() error { return nil }

// Signal returns a pseudo signal strength generated through a sine function
// based on time.
func (l *pseudoLink) Signal() int {
	t := time.Now().Sub(l.start).Seconds()
	return int((30.0*math.Sin((t+7.0)/2.0) - 50.0))
}

// Quality is only here to satisfy Link interface; no behaviour.
func (l *pseudoLink) Quality() int { return 0 }

// Noise is only here to satisfy Link interface; no behaviour.
func (l *pseudoLink) Noise() int { return 0 }

// Bitrate is only here to satisfy Link interface; no behaviour.
func (l *pseudoLink) Bitrate() int { return 0 }
