/*
NAME
  options.go provides netsender client initialisation options.

AUTHORS
  Saxon Nelson-Milton <saxon@ausocean.org>

LICENSE
  netsender is Copyright (C) 2017-2019 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with netsender in gpl.txt. If not, see http://www.gnu.org/licenses.
*/

package netsender

import (
	"encoding/json"
	"fmt"
	"strings"
)

// Option is the function signature returned by option functions below for
// use in the netsender initialiser.
type Option func(*Sender) error

// WithVarTypes returns an Option that sets the Sender var types details.
func WithVarTypes(vt map[string]string) Option {
	return func(s *Sender) error {
		// Validate the varTypes.
		for key, val := range vt {
			switch val {
			case "string", "bool", "int", "uint", "float":
				continue
			default:
				var enums []string
				if strings.HasPrefix(val, "enum:") {
					enums = strings.Split(val[5:], ",")
				}
				if strings.HasPrefix(val, "enums:") {
					enums = strings.Split(val[6:], ",")
				}
				if len(enums) > 1 {
					continue
				}
			}
			return fmt.Errorf("invalid variable type: key %s has invalid value: %s", key, val)
		}

		la := localAddr()
		vtBytes, err := json.Marshal(vt)
		if err != nil {
			return fmt.Errorf("could not marshal var type map: %w", err)
		}

		// Create pins for var types (vt) and local address (la).
		s.mu.Lock()
		s.configPins = []Pin{
			Pin{
				Name:     "vt",
				Value:    len(vtBytes),
				Data:     vtBytes,
				MimeType: "application/json",
			},
			Pin{
				Name:  "la",
				Value: len(la),
				Data:  []byte(la),
			},
		}
		s.mu.Unlock()

		return nil
	}
}

// WithConfigFile returns an option that sets a custom netsender config file path.
func WithConfigFile(f string) Option {
	return func(s *Sender) error {
		s.configFile = f
		return nil
	}
}

// WithUpgrader returns an option that sets the upgrader function which is called
// when netsender.Sender.Upgrade() is called.
func WithUpgrader(u func(tag, user, gopath string) error) Option {
	return func(s *Sender) error {
		s.upgrader = u
		return nil
	}
}
