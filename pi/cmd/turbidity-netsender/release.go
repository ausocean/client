// +build !test

/*
DESCRIPTION
  release.go is the default build and will be used when 'test' tag is absent.

AUTHORS
  Harrison Telford <harrison@ausocean.org>

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

// RaspiSerialPacket is left empty here in order to allow build in Circleci
// when 'test' tag is absent.
func RaspiSerialPacket() {
}

// DummyValuePacket is left empty here in order to allow build in Circleci
// when 'test' tag is absent.
func DummyValuePacket() {
}
