/*
DESCRIPTION
  servo.go provides an implementation of the ServoMotor interface for a basic
  0-180 degree servo using a background python process responsible for the
  hardware interfacing.

AUTHORS
  Saxon Nelson-Milton <saxon@ausocean.org>

LICENSE
  Copyright (C) 2020 the Australian Ocean Lab (AusOcean)

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
	"io"
	"os/exec"
	"strconv"
	"time"

	"github.com/ausocean/utils/logging"
)

// Background process constants.
const processStartWait = 5 * time.Second

// Servo is an implementation of the ServoMotor interface for a standard 0-180
// degree servo.
type Servo struct {
	cmd    *exec.Cmd
	stdin  io.Writer
	stdout io.Reader
	angle  int
	log    logging.Logger
}

// NewServo returns a new servo motor with signal pin number provided.
func NewServo(pin int, l logging.Logger) (*Servo, error) {
	s := &Servo{log: l}
	s.cmd = exec.Command(python, "-c", servoScript, strconv.Itoa(pin))

	var err error
	s.stdin, err = s.cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("could not pipe stdin of process: %w", err)
	}

	s.stdout, err = s.cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("could not pipe stdout of process: %w", err)
	}

	s.log.Debug("starting servoCommand process")
	err = s.cmd.Start()
	if err != nil {
		return nil, fmt.Errorf("could not start servCommand process: %w", err)
	}

	time.Sleep(processStartWait)

	return s, nil
}

// Move moves the servo using the servo command background process.
func (s *Servo) Move(a int) error {
	if a < 0 {
		a = 0
	} else if a > 180 {
		a = 180
	}
	s.angle = a
	s.log.Debug("received move command")
	str := strconv.Itoa(a)
	_, err := s.stdin.Write([]byte(str + "\r\n"))
	if err != nil {
		return fmt.Errorf("could not write angle to servoCommand process: %w", err)
	}
	return nil
}

// Angle returns the currently angle of the servo motor.
func (s *Servo) Angle() int {
	return s.angle
}

// Shutdown kills the servo command background process.
func (s *Servo) Shutdown() error {
	s.log.Debug("shutting down")
	err := s.cmd.Process.Kill()
	if err != nil {
		return fmt.Errorf("could not kill servoCommand process: %w", err)
	}
	return nil
}

const servoScript = `
# DESCRIPTION
#  servoCommand.py takes servo angle commands from stdin and writes the appropriate
#  PWM to the connected servo.
#
# AUTHORS
#   Saxon A. Nelson-Milton <saxon@ausocean.org>
#   Ella Pietroria <ella@ausocean.org>
#
# LICENSE
#   Copyright (C) 2020-2021 the Australian Ocean Lab (AusOcean)
#
#   It is free software: you can redistribute it and/or modify them
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   It is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
#   for more details.
#
#   You should have received a copy of the GNU General Public License
#   in gpl.txt.  If not, see ht
# DESCRIPTION
#  servoCommand.py takes servo angle commands from stdin and writes the appropriate
#  PWM to the connected servo.
#
# AUTHORS
#   Saxon A. Nelson-Milton <saxon@ausocean.org>
#   Ella Pietroria <ella@ausocean.org>
#
# LICENSE
#   Copyright (C) 2020-2021 the Australian Ocean Lab (AusOcean)
#
#   It is free software: you can redistribute it and/or modify them
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   It is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
#   for more details.
#
#   You should have received a copy of the GNU General Public License
#   in gpl.txt.  If not, see http://www.gnu.org/licenses.

import sys
import logging

# Logging settings.
LOG_TO_FILE = False
LOG_FILE = "servoCommand.log"
LOG_LEVEL = logging.DEBUG

# Set up logging.
if LOG_TO_FILE:
    logging.basicConfig(filename=LOG_FILE,level=LOG_LEVEL)
else:
    logging.basicConfig(stream=sys.stdout,level=LOG_LEVEL)

# PWM parameters.
CENTRE_POSITION_PWM = 1500 # Centre position corresponds to approximately 90 degrees.
PWM_FREQUENCY = 50

# Hardware pin the servo signal line is connected to.
DEFAULT_SERVO_PIN = 14

# Max and min PWM widths i.e. those corresponding to 180 and 0 degrees.
# NOTE: servos will have 0 and 180 degree angles correspond to different widths.
# The widths must be altered for each new servo used.
# DAMAGE MAY BE INCURED IF THIS IS NOT PERFORMED!!!
MAX_WIDTH = 2500
MIN_WIDTH = 500

# The factor used to calculate width from an angle.
# NOTE: this will also need to be adjusted for each new servo.
# DAMAGE MAY BE INCURED IF THIS IS NOT PERFORMED!!!
BEARING_TO_WIDTH = 10.81

# angleToWidth returns the PWM width corresponding to the given angle.
def angleToWidth(bearing):
    return BEARING_TO_WIDTH*bearing

# First we try to use a real servo. If this doesn't work, we employ a dummy servo.
try:
    import pigpio
    class Servo:
        def __init__(self,servoPin):
            self.servoPin = servoPin
            self.pwm = pigpio.pi()
            self.pwm.set_mode(self.servoPin, pigpio.OUTPUT)
            self.pwm.set_PWM_frequency(self.servoPin, PWM_FREQUENCY)
            self.pwm.set_servo_pulsewidth(self.servoPin,CENTRE_POSITION_PWM)
            self.angle = 90

        def move(self,angle):
            width = MIN_WIDTH + angleToWidth(angle)
            self.pwm.set_servo_pulsewidth(self.servoPin,max(min(MAX_WIDTH, width), MIN_WIDTH))

except:
    class Servo:
        def __init__(self,servoPin):
            pass

        def move(self,angle):
            width = MIN_WIDTH + angleToWidth(angle)
            print("setting PWM to: ",max(min(MAX_WIDTH, width), MIN_WIDTH))
            sys.stdout.flush()

def main():
    # Check if pin argument has been provided, otherwise default.
    pin = DEFAULT_SERVO_PIN
    if len(sys.argv)-1 > 0:
        logging.debug("custom pin: %d",pin)
        pinStr = sys.argv[1]
        try:
            pin = int(pinStr)
        except:
            pin = DEFAULT_SERVO_PIN

    logging.debug("creating servo")
    s = Servo(pin)
    for line in sys.stdin:
        logging.debug("got angle command: %s",line)
        line.rstrip()
        try:
            angle = int(line)
        except ValueError:
            logging.debug("value error")
            continue
        logging.debug("moving servo")
        s.move(angle)


if __name__ == "__main__":
    main()


`
