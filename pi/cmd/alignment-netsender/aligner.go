/*
DESCRIPTION
  aligner.go provides a functionality for the maintenenance of alignment
  of a CPE using a compass (magnetometer) and a servo motor.

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
package main

import (
	"encoding/csv"
	"errors"
	"fmt"
	"math"
	"os"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/ausocean/client/pi/cmd/alignment-netsender/calibration"
	"github.com/ausocean/utils/logging"
)

// If true, saves plots of data to file. Can be set to true with "plot" tag.
var canPlot = true

// Servo consts.
const (
	servoPin             = 14
	defaultAdjustIntvl   = 300 * time.Millisecond
	defaultSweepIncDelay = 50 * time.Millisecond
	defaultServoAngle    = 90
	sweepInc             = 1
)

// Controller constants.
const (
	defaultCoeff    = 0.5
	defaultThres    = 3
	defaultRefAngle = 90
)

// Amount of time to wait before doing anything if in error state.
const errStateWait = 5 * time.Second

// Filename for calibration results storage.
const calFileName = "cal.csv"

// Sweep init and finish positions.
const (
	sweepInitPos   = 0
	sweepFinishPos = 180
)

// Python command to run child process scripts.
const python = "python3"

// Magnetometer represents electronic magnetometer hardware from which x and y
// axis magnetic field values can be retrieved along with any errors.
// Shutdown may be used for any clean up ops.
type Magnetometer interface {
	Values() (float64, float64, float64, error)
	Shutdown() error
}

// ServoMotor is  a servo motor that will rotate to the angle given to the Move
// method. The Angle method returns the current angle at which the ServoMotor
// is at. Shutdown is intended for any clean up ops.
type ServoMotor interface {
	// Move moves the servo motor to the given angle in degrees.
	Move(angle int) error

	// Angle returns the current angle in degrees.
	Angle() int

	// Shutdown performs any clean up operations.
	Shutdown() error
}

// Link represents the network link between the CPE fitted to the aligner and
// a base station with which communication occurs.
type Link interface {
	// Update is call where manual update of stats is required.
	Update() error

	// Signal returns the signal strength in dB.
	Signal() int

	// Quality returns the quality of the link in percentage.
	Quality() int

	// Noise returns the noise in the link in dB.
	Noise() int

	// Bitrate returns the link bitrate in kbits/s.
	Bitrate() int
}

// CPEAligner provides functionality for the maintenance of alignment of a
// CPE WIFI router system using a magnetometer and servo motor.
type CPEAligner struct {
	mag           Magnetometer
	servo         ServoMotor
	link          Link
	cal           *calibration.Results // Holds latest fitted claibration results.
	adjustIntvl   time.Duration        // Holds servo adjustment interval.
	sweepIncDelay time.Duration        // Time between sweep increments.
	adjustTicker  *time.Ticker         // Used to periodically adjust servo for alignment.
	err           bool                 // If true, indicates the aligner is in an error state.
	log           logging.Logger
	calSignal     chan struct{}

	mu       sync.Mutex
	refAngle float64     // Holds a reference servo angle that corresponded to best CPE position.
	ctrl     *controller // Controller for determining servo correction.
}

// NewCPEAligner returns a new CPEAligner adopting the provided logging.Logger
// for logging throughout operation.
func NewCPEAligner(l logging.Logger, link Link) (*CPEAligner, error) {
	m, err := NewLSM303Magnetometer(l)
	if err != nil {
		return nil, fmt.Errorf("could not create magnetometer: %w", err)
	}

	s, err := NewServo(servoPin, l)
	if err != nil {
		return nil, fmt.Errorf("could not create servo: %w", err)
	}

	return &CPEAligner{
		ctrl:          newController(defaultCoeff, defaultThres),
		refAngle:      defaultRefAngle,
		log:           l,
		mag:           m,
		servo:         s,
		link:          link,
		adjustIntvl:   defaultAdjustIntvl,
		sweepIncDelay: defaultSweepIncDelay,
		adjustTicker:  time.NewTicker(defaultAdjustIntvl),
		calSignal:     make(chan struct{}),
	}, nil
}

// Align will perform actions based on two possible signals i.e. calibration and
// aligner adjustment. Calibration is triggered when the CPEAligner.Calibrate
// method is called. The aligner adjustment signal is based on a ticker that
// will periodically cause calculation of misalignment and actuation of the
// servo for correction.
// Align is intended to be called as a routine.
func (a *CPEAligner) Align() {
	// Alignment loop.
	for {
		select {
		case <-a.calSignal:
			a.log.Info("got calibrate signal")
			err := a.calibrate()
			if err != nil {
				a.errState("could not calibrate", "error", err)
				continue
			}
			a.log.Info("calibrated", "refAngle", a.refAngle)

		case <-a.adjustTicker.C:
			if a.err {
				a.err = false
				a.adjustTicker.Reset(a.AdjustIntvl())
			}

			// If calibration is nil, try to load from file.
			if a.cal == nil {
				a.log.Info("no calibration, trying to load from file")
				err := a.loadCalibration()
				if err != nil {
					a.errState("could not load calibration data", "error", err)
					continue
				}
				a.log.Info("loaded calibration from file")
			}

			// Check alignment and correct if off.
			err := a.checkAlignment()
			if err != nil {
				a.errState("could not check/correct alignment", "error", err)
			}
		}
	}
}

// calibrate causes the CPE aligner to perform a sweep from which servo angle,
// magnetometer and signal strength stats are collected. Polynomials are then
// fitted to the data and "best signal" angle is derived to be set as a
// reference angle. The fitted calibration data is finally saved.
func (a *CPEAligner) calibrate() error {
	res, err := a.Sweep()
	if err != nil {
		return fmt.Errorf("could not sweep: %w", err)
	}

	if canPlot {
		err = calibration.PlotRawResults(res)
		if err != nil {
			return fmt.Errorf("could not plot sweep results: %w", err)
		}
	}

	a.cal, _, err = res.Fit()
	if err != nil {
		return fmt.Errorf("could not fit data: %w", err)
	}

	if canPlot {
		err = calibration.PlotFitResults(res, a.cal)
		if err != nil {
			return fmt.Errorf("could not plot fit results: %w", err)
		}
	}

	ref, err := a.cal.BestSignalAngle()
	if err != nil {
		return fmt.Errorf("could not get servo angle corresponding to best signal: %w", err)
	}
	a.log.Info("got best signal angle", "angle", ref)

	err = a.SetRefAngle(ref)
	if err != nil {
		return fmt.Errorf("could not set reference servo angle: %w", err)
	}

	err = a.saveCalibration()
	if err != nil {
		return fmt.Errorf("could not save calibration data: %w", err)
	}
	return nil
}

// errState will log the provided error information, move the servo to the
// default servo position (should be most optimised position, next to one
// based on calibration data) and set the adjustTicker timer to wait for
// errStateWait seconds before next alignment check. Therefore within this time,
// a re-calibration can be performed if requested.
func (a *CPEAligner) errState(msg string, args ...interface{}) {
	a.log.Error(msg, args...)
	err := a.servo.Move(defaultServoAngle)
	if err != nil {
		a.log.Error("could not move servo to error state position", "error", err)
	}
	a.adjustTicker.Reset(errStateWait)
	a.err = true
}

// Sweep moves the servo from 0 to 180 incrementally while collecting magnetometer
// and signal strength readings for each increment. This data is stored in a
// calibration.Results value that is returned.
func (a *CPEAligner) Sweep() (*calibration.Results, error) {
	res := calibration.NewResults(0)

	err := a.servo.Move(sweepInitPos)
	if err != nil {
		return nil, fmt.Errorf("could not move aligner to sweep start position: %w", err)
	}

	// Wait for servo to finis moving from prior position to 0 degrees (to avoid
	// substantial magnetometer reading noise).
	const sweepInitWait = 3 * time.Second
	time.Sleep(sweepInitWait)

	for ang := 0; ang < sweepFinishPos; ang += sweepInc {
		err := a.servo.Move(ang)
		if err != nil {
			return nil, fmt.Errorf("could not move servo to position: %d: %w", ang, err)
		}

		// Get signal and mag readings.
		signal, err := a.LinkSignal()
		if err != nil {
			return nil, fmt.Errorf("could not get link signal: %w", err)
		}
		x, y, _, err := a.mag.Values()
		if err != nil {
			return nil, fmt.Errorf("could not get magnetometer readings: %w", err)
		}
		a.log.Debug("got mag x and y, and signal", "x", x, "y", y, "signal(db)", signal)

		// Add results to calibration.Results value.
		res.Add(float64(ang), x, y, float64(signal))
		a.log.Debug("step complete", "progress(%)", (100*ang)/180)
		time.Sleep(a.SweepIncDelay())
	}

	return res, nil
}

// saveCalibration saves the magnetometer calibration data stored in the
// CPEAligner.cal field to file as CSV.
func (a *CPEAligner) saveCalibration() error {
	if a.cal == nil {
		panic("no calibration data to save")
	}

	f, err := os.Create(calFileName)
	if err != nil {
		return err
	}
	defer f.Close()

	w := csv.NewWriter(f)
	defer w.Flush()

	// Write the first entry to provide reference angle.
	err = w.Write([]string{fmt.Sprintf("%f", a.RefAngle()), "0.0", "0.0"})
	if err != nil {
		return fmt.Errorf("could not write referenace angle to calibration file: %w", err)
	}

	for i := range a.cal.Angles {
		in := strings.Split(fmt.Sprintf("%f,%f,%f", a.cal.Angles[i], a.cal.MagX[i], a.cal.MagY[i]), ",")
		err := w.Write(in)
		if err != nil {
			return fmt.Errorf("could not write calibration entry: %d:%w", i, err)
		}
	}
	return nil
}

// Check alignment retrieves the currently read magnetometer axis values, derives
// a corresponding angle in degrees and uses a controller to compare to the
// target angle and provide an output value for servo adjustment.
func (a *CPEAligner) checkAlignment() error {
	// Get magnetometer axis readings.
	x, y, _, err := a.mag.Values()
	if err != nil {
		return fmt.Errorf("error from magnetometer: %w", err)
	}
	a.log.Debug("got mag values", "x", x, "y", y)

	// Find corresponding servo angle for given mag values.
	ang, err := a.cal.AngleFromMag(x, y)
	if err != nil {
		return fmt.Errorf("could not find angle for magnetometer values: %w", err)
	}
	a.log.Debug("got angle from mag readings", "ang", ang)

	// Send through controller.
	out := a.ctrl.output(a.RefAngle(), ang)
	a.log.Debug("got controller out", "out", out)

	angle := a.servo.Angle() + int(math.Round(out))
	a.log.Debug("calculated new angle", "angle", angle)

	err = a.servo.Move(angle)
	if err != nil {
		return fmt.Errorf("could not move servo to correct misalignment: %w", err)
	}

	return nil
}

// Load calibration loads calibration reference angle and servo angle/magnetometer
// data from file.
func (a *CPEAligner) loadCalibration() error {
	f, err := os.Open(calFileName)
	if err != nil {
		return fmt.Errorf("could not open calibration file: %w", err)
	}
	defer f.Close()

	lines, err := csv.NewReader(f).ReadAll()
	if err != nil {
		return fmt.Errorf("could not read calibration lines: %w", err)
	}

	refAng, err := strconv.ParseFloat(lines[0][0], 64)
	if err != nil {
		return fmt.Errorf("could not parse reference angle string to float: %w", err)
	}

	err = a.SetRefAngle(refAng)
	if err != nil {
		return fmt.Errorf("could not set reference angle: %w", err)
	}

	lines = lines[1:]
	a.cal = calibration.NewResults(len(lines))
	var vals [3]float64
	for i, line := range lines {
		for j := range vals {
			vals[j], err = strconv.ParseFloat(line[j], 64)
			if err != nil {
				return fmt.Errorf("could not parse cal val: %d: from line: %v: %w", i, line, err)
			}
		}
		a.cal.Angles[i] = vals[0]
		a.cal.MagX[i] = vals[1]
		a.cal.MagY[i] = vals[2]
	}
	return nil
}

// LinkSignal returns the CPEAligner link strength in dB.
func (a *CPEAligner) LinkSignal() (int, error) { return a.getLinkStat(a.link.Signal) }

// LinkQuality returns the CPEAligner link quality (unknown unit).
func (a *CPEAligner) LinkQuality() (int, error) { return a.getLinkStat(a.link.Quality) }

// LinkNoise returns the CPEAligner link noise in dB.
func (a *CPEAligner) LinkNoise() (int, error) { return a.getLinkStat(a.link.Noise) }

// LinkBitrate returns the CPEAligner link bitrate (kbits/s).
func (a *CPEAligner) LinkBitrate() (int, error) { return a.getLinkStat(a.link.Bitrate) }

// getLinkStats refreshes the link stats and uses the provided function to get
// the required statistic.
func (a *CPEAligner) getLinkStat(stat func() int) (int, error) {
	err := a.updateLink()
	if err != nil {
		return 0, fmt.Errorf("could not update link: %w", err)
	}
	return stat(), nil
}

// updateLink calls Link.Update on the CPEAligner's Link to refresh the link stats.
func (a *CPEAligner) updateLink() error {
	if reflect.ValueOf(a.link).IsNil() {
		return errors.New("link not initialised")
	}
	return a.link.Update()
}

// SetLinkConfig takes a string of form "<iface>,<ip>,<user>,<pass>".
// Where,
// iface = the network interface connected to the aligner CPE.
// ip = the IP address of the aligner CPE gateway.
// user = the root username for login.
// pass = the root password for login.
func (a *CPEAligner) SetLinkConfig(c string) error {
	r, err := csv.NewReader(strings.NewReader(c)).Read()
	if err != nil {
		return fmt.Errorf("could not decode config string: %w", err)
	}

	a.link, err = newLink(r[0], r[1], r[2], r[3], r[4])
	if err != nil {
		return fmt.Errorf("could not create link with new config: %w", err)
	}
	return nil
}

// Calibrate signals the CPEAligner.Align routine to perform a calibration.
func (a *CPEAligner) Calibrate() {
	a.calSignal <- struct{}{}
}

// RefAngle returns the currently used reference angle for correction calculation.
// Concurrency safe.
func (a *CPEAligner) RefAngle() float64 {
	a.mu.Lock()
	ra := a.refAngle
	a.mu.Unlock()
	return ra
}

// ErrorStdDev returns the standard deviation of the error between feedback and
// target. This is indicative of noise in the feedback signal.
// Concurrency safe.
func (a *CPEAligner) ErrorStdDev() float64 {
	a.mu.Lock()
	sd := a.ctrl.errSD.value
	a.mu.Unlock()
	return sd
}

// MedianError returns the median vaue of the error between feedback and
// target. This is indicative of how well the CPEAligner is working.
// Significantly large values indicate a problem.
// Concurrency safe.
func (a *CPEAligner) MedianError() float64 {
	a.mu.Lock()
	me := a.ctrl.errMed.value
	a.mu.Unlock()
	return me
}

// SetRefAngle allows for manual setting of the CPEAligners reference angle.
// Concurrency safe.
func (a *CPEAligner) SetRefAngle(t float64) error {
	if t < 0 || 180 < t {
		return errors.New("target not within valid range of 0-180 degrees")
	}
	a.mu.Lock()
	a.refAngle = t
	a.mu.Unlock()
	return nil
}

// SetThreshold set's the CPEAligner's controller error threshold i.e. the min
// error before correction occurs.
// Concurrency safe.
func (a *CPEAligner) SetThreshold(t float64) error {
	return a.ctrl.setThreshold(t)
}

// SetAdjustIntvl sets the delay between alignment checks.
// Concurrency safe.
func (a *CPEAligner) SetAdjustIntvl(s int) {
	a.mu.Lock()
	a.adjustIntvl = time.Duration(s) * time.Millisecond
	a.adjustTicker.Reset(a.adjustIntvl)
	a.mu.Unlock()
}

// SetSweepIncDelay sets the delay between sweep increments.
// This is given in milliseconds.
func (a *CPEAligner) SetSweepIncDelay(d int) {
	a.mu.Lock()
	a.sweepIncDelay = time.Duration(d) * time.Millisecond
	a.mu.Unlock()
}

// AdjustIntvl returns the delay between alignment checks.
// Concurrency safe.
func (a *CPEAligner) AdjustIntvl() time.Duration {
	a.mu.Lock()
	s := a.adjustIntvl
	a.mu.Unlock()
	return s
}

// SweepIncDelay returns the delay between sweep increments.
func (a *CPEAligner) SweepIncDelay() time.Duration {
	a.mu.Lock()
	d := a.sweepIncDelay
	a.mu.Unlock()
	return d
}

// SetGain sets the controller gain.
// Concurrency safe.
func (a *CPEAligner) SetGain(c float64) error {
	return a.ctrl.setGain(c)
}

// Shutdown will signal to the Align routine to terminate, and then Shutdown
// the compass and servo components.
func (a *CPEAligner) Shutdown() error {
	if a.calSignal != nil {
		close(a.calSignal)
	}

	err := a.mag.Shutdown()
	if err != nil {
		return fmt.Errorf("could not shutdown compass: %w", err)
	}

	err = a.servo.Shutdown()
	if err != nil {
		return fmt.Errorf("could not shutdown servo: %w", err)
	}
	return nil
}
