//go:build !nocpe
// +build !nocpe

/*
DESCRIPTION
  cpe.go provides an initialisation function for a Link.

AUTHORS
  Saxon Nelson-Milton <saxon@ausocean.org>

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

import "github.com/ausocean/utils/link"

// newLink returns a Link with an underlying type of link.Link, a type designed
// with OPENWRT flashed hardware in mind, to retrieve network statistics.
func newLink(device, ip, port, user, pass string) (Link, error) {
	return link.New(device, ip, port, user, pass)
}
