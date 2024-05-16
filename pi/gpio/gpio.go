/*
DESCRIPTION
  Provides functions for writing/reading on Raspberry Pi GPIO pins that implement the
  netsender PinInit and PinReadWrite function signatures.

AUTHOR
  Jack Richardson <richardson.jack@outlook.com>
  Saxon Nelson-Milton <saxon@ausocean.org>

LICENSE
  Copyright (C) 2017-2020 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with revid in gpl.txt.  If not, see [GNU licenses](http://www.gnu.org/licenses).
*/

// Package gpio provides functions for writing/reading GPIO pins on the Raspberry Pi.
package gpio

import (
	"errors"
	"fmt"
	"strconv"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/sds"
	"github.com/kidoman/embd"
	"github.com/kidoman/embd/convertors/mcp3008"
)

// SPI bus properties.
const (
	spiMode    = embd.SPIMode0
	spiChannel = 0
	spiSpeed   = 1000000
	spiBPW     = 0
	spiDelay   = 0
)

var (
	// Analog to digital converter.
	adc *mcp3008.MCP3008

	// Keep track of initialisation state.
	initialised = false
)

// InitGPIOPin firstly initialises GPIO drivers if not done yet, and then sets the
// direction of GPIO pin given the direction through the data parameter, which can
// be set as one of the two consts PinIn or PinOut.
func InitPin(pin *netsender.Pin, data interface{}) error {
	err := initGPIO()
	if err != nil {
		return fmt.Errorf("GPIO initialisation failed: %w", err)
	}

	switch pin.Name[0] {
	case 'A':
		// do nothing
	case 'D':
		// Get the number of the pin.
		pn, err := strconv.Atoi(pin.Name[1:])
		if err != nil {
			return err
		}

		// Get the direction of the pin i.e. in or out.
		dir, ok := data.(int)
		if !ok {
			return errors.New("expected data to be an int")
		}

		switch dir {
		case netsender.PinIn:
			err = embd.SetDirection(pn, embd.In)
			if err != nil {
				return err
			}
		case netsender.PinOut:
			err = embd.SetDirection(pn, embd.Out)
			if err != nil {
				return err
			}
		default:
			return fmt.Errorf("invalid pin direction: %d", dir)
		}
	case 'X':
		// do nothing
	}
	return nil
}

// ReadGPIOPin reads the GPIO pin corresponding to pin.Name.
func ReadPin(pin *netsender.Pin) error {
	pn, err := strconv.Atoi(pin.Name[1:])
	if err != nil {
		return err
	}
	var val int
	switch pin.Name[0] {
	case 'A':
		var adc *mcp3008.MCP3008
		val, err = adc.AnalogValueAt(pn)
	case 'D':
		val, err = embd.DigitalRead(pn)
		if err != nil {
			return err
		}
	case 'X':
		return sds.ReadSystem(pin)
	default:
		return errors.New("invalid pin type: " + pin.Name)
	}
	pin.Value = val
	return err
}

// WriteGPIOPin writes to the GPIO pin corresponding to pin.Name, using
// the value in pin.Value.
func WritePin(pin *netsender.Pin) error {
	pn, err := strconv.Atoi(pin.Name[1:])
	if err != nil {
		return err
	}

	switch pin.Name[0] {
	case 'A':
		return errors.New("writing to A pin not implemented")
	case 'D':
		if pin.Value == 0 {
			err = embd.DigitalWrite(pn, embd.Low)
			if err != nil {
				return err
			}
		} else {
			err = embd.DigitalWrite(pn, embd.High)
			if err != nil {
				return err
			}
		}
	case 'X':
		return errors.New("writing to X pin not implemented")

	default:
		return errors.New("invalid pin type: " + pin.Name)
	}
	return nil
}

// init initialised GPIO and SPI drivers as well as an analog to digital converter for
// reading analog values. If initialisation has already occured we return nil immediately.
func initGPIO() error {
	if initialised {
		return nil
	}

	err := embd.InitGPIO()
	if err != nil {
		return fmt.Errorf("could not initialise GPIO drivers: %w", err)
	}

	err = embd.InitSPI()
	if err != nil {
		return fmt.Errorf("could not initialise SPI drivers: %w", err)
	}

	spiBus := embd.NewSPIBus(
		spiMode,
		spiChannel,
		spiSpeed,
		spiBPW,
		spiDelay,
	)
	adc = mcp3008.New(mcp3008.SingleMode, spiBus)

	initialised = true
	return nil
}
