/*
DESCRIPTION
  controller.go provides a controller for calculating required aligner correction
  angle based on the target angle and feedback signal. Other metrics are calculated
  and available, namely standard deviation of error, and median error.

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
	"fmt"
	"math"
	"sort"
	"sync"

	"gonum.org/v1/gonum/stat"
)

// Filter consts.
const (
	medianWindow  = 10 // Running median window size.
	averageWindow = 4  // Running average window size.
	stdDevWindow  = 100
)

// Controller tuning consts.
const (
	minGain      = 0.001
	minThreshold = 0
	maxThreshold = 180
)

// controller is a controller used to calculate appropriate angle correction for
// the CPEAligner based on a target and feedback signal. This is a proportional
// controller implementation and includes input signal smoothing on the error
// signal to remove noise.
type controller struct {
	mu sync.Mutex

	g float64 // Gain.
	t float64 // Error threshold for correction.

	// Running calculations.
	errAvg *runningAverage
	errMed *runningMedian
	errSD  *runningStdDev
}

// newController creates a new controller with proportional controller gain
// g, and error correction threshold of t. The threshold is the minimum value
// at which the controller will correct.
func newController(g, t float64) *controller {
	return &controller{
		g:      g,
		t:      t,
		errAvg: &runningAverage{n: averageWindow},
		errMed: newRunningMedian(medianWindow),
		errSD:  newRunningStdDev(stdDevWindow),
	}
}

// output provides the controller output given the target, t and feedback signal,
// f. Standard deviation of error is calculated to indicate noise.
func (c *controller) output(t, f float64) float64 {
	diff := t - f

	// Update moving average filter.
	c.errAvg.update(diff)

	// Update error median and standard deviation with concurrency in mind given
	// these will be requested external to the control loop.
	c.mu.Lock()
	c.errMed.update(diff)
	c.errSD.update(diff)
	c.mu.Unlock()

	// If error is above threshold, return calculated output for correction.
	if math.Abs(c.errAvg.value) > c.threshold() {
		return c.gain() * float64(c.errAvg.value)
	}
	return 0
}

// setCoefficient sets the controllers gain.
func (c *controller) setGain(g float64) error {
	if g < minGain {
		return fmt.Errorf("inappropriate gain value: %f", g)
	}
	c.mu.Lock()
	c.g = g
	c.mu.Unlock()
	return nil
}

// gain returns the current controller gain.
func (c *controller) gain() float64 {
	c.mu.Lock()
	g := c.g
	c.mu.Unlock()
	return g
}

// setThreshold sets the error threshold for correction.
func (c *controller) setThreshold(t float64) error {
	if t < minThreshold || maxThreshold < t {
		return fmt.Errorf("inappropriate threshold value: %f", t)
	}
	c.mu.Lock()
	c.t = t
	c.mu.Unlock()
	return nil
}

// threshold returns the current error threshold for correction value.
func (c *controller) threshold() float64 {
	c.mu.Lock()
	t := c.t
	c.mu.Unlock()
	return t
}

// runningStdDev calculates and holds a running standard deviation.
type runningStdDev struct {
	win     []float64
	n, i, l int
	value   float64
}

// newRunningStdDev returns a new runningStdDev with calculation window size of n.
func newRunningStdDev(n int) *runningStdDev {
	return &runningStdDev{n: n, win: make([]float64, n)}
}

// update updates the running standard deviation with value, v.
func (sd *runningStdDev) update(v float64) {
	sd.win[sd.i] = v
	sd.i = (sd.i + 1) % sd.n

	if sd.l != sd.n {
		sd.l++
	}

	// Calculate mean of window.
	if sd.l == 1 {
		sd.value = 0
		return
	}

	_, sd.value = stat.MeanStdDev(sd.win[:sd.l], nil)
}

// median holds a running median calculation.
type runningMedian struct {
	win     []float64
	value   float64
	sorted  []float64
	n, l, i int
}

// newRunningMedian returns a new runningMedian with calculation window size of n.
func newRunningMedian(n int) *runningMedian {
	return &runningMedian{n: n, sorted: make([]float64, n), win: make([]float64, n)}
}

// update updates the running median calculation using the provided value, v.
func (m *runningMedian) update(v float64) {
	m.win[m.i] = v
	m.i = (m.i + 1) % m.n

	if m.l != m.n {
		m.l++
	}

	copy(m.sorted, m.win)
	sort.Float64s(m.sorted[:m.l])

	if m.l%2 == 0 {
		m.value = (m.sorted[:m.l][(m.l/2)-1] + m.sorted[:m.l][m.l/2]) / 2.0
	} else {
		m.value = m.sorted[:m.l][(m.l / 2)]
	}
}

// runningAvg calculates and holds a running average.
type runningAverage struct{ n, value float64 }

/// update updates the running average with value u
func (a *runningAverage) update(u float64) { a.value = (a.value * (a.n - 1) / a.n) + u/a.n }
