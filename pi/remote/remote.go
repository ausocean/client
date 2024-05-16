/*
AUTHORS
  Trek Hopton <trek@ausocean.org>

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
  in gpl.txt.  If not, see http://www.gnu.org/licenses.
*/

// Package remote provides a type and methods representing a network connected
// device that can be interacted with via SSH.
package remote

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"net"
	"strconv"
	"time"

	"golang.org/x/crypto/ssh"

	"github.com/ausocean/utils/logging"
)

// Network configuration.
const (
	defaultSSHPort = 22
	logPort        = 5555
	logProtocal    = "tcp"
	logBufSize     = 1024
)

// Remote represents a remote device that can be accessed via SSH.
type Remote struct {
	user      string
	pass      string
	ipAddr    string
	port      int
	conn      *ssh.Client
	connected bool
}

// New returns a new Remote with the provided username, password and device IP address.
func New(user, pass, ip string) *Remote {
	return &Remote{user: user, pass: pass, port: defaultSSHPort, ipAddr: ip, connected: false}
}

// Connect opens an SSH connection with the remote device using the current configuration.
// If a connection is already open, it will be kept open and no error will be returned.
// If an error is returned, it should be assumed that no connection was made.
func (r *Remote) Connect() error {
	if r.connected {
		return nil
	}
	cfg := &ssh.ClientConfig{
		User: r.user,
		Auth: []ssh.AuthMethod{
			ssh.Password(r.pass),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	var err error
	r.conn, err = ssh.Dial("tcp", r.ipAddr+":"+strconv.Itoa(r.port), cfg)
	if err != nil {
		return err
	}
	r.connected = true

	return nil
}

// Disconnect closes the SSH connection to the remote device.
// If no connection exists, this function will return without error.
func (r *Remote) Disconnect() error {
	if !r.connected {
		return nil
	}
	err := r.conn.Close()
	if err != nil {
		return fmt.Errorf("disconnect failed: %w", err)
	}
	r.connected = false
	return nil
}

// Exec executes a given command on the remote device and returns the output
// as a string. If the command fails, the given timeout elapses, or an SSH connection has not been opened
// using Connect(), an error will be returned with an empty string.
func (r *Remote) Exec(command string, timeout time.Duration) (string, error) {
	if timeout < 1 {
		return "", errors.New("timeout must be valid")
	}
	if !r.connected {
		return "", errors.New("no SSH connection established to remote device")
	}

	session, err := r.conn.NewSession()
	if err != nil {
		return "", fmt.Errorf("failed to begin SSH session: %w", err)
	}
	defer session.Close()

	t := time.NewTimer(timeout)
	resCh := make(chan string)
	errCh := make(chan error)

	go func() {
		output, err := session.CombinedOutput(command)
		if err != nil {
			errCh <- err
		}
		resCh <- string(output)
	}()

	select {
	case err := <-errCh:
		return "", fmt.Errorf("executing command resulted in error: %w", err)
	case ms := <-resCh:
		return ms, nil
	case <-t.C:
		return "", fmt.Errorf("executing command timed out after %v seconds", timeout.Seconds())
	}
}

// Listen continually runs listening and logging syslogs sent via TCP and addressed
// to the given IP address, to the given logger.
// Messages will also be logged to the given logger from within this function, including errors that occur.
func Listen(l logging.Logger, ip string) {
	ln, err := net.Listen(logProtocal, ip+":"+strconv.Itoa(logPort))
	if err != nil {
		l.Error("error listening for connections", "error", err)
		return
	}
	defer ln.Close()

	l.Info("listening", "IP", ip, "port", logPort)
	for {
		l.Info("waiting for connection")
		conn, err := ln.Accept()
		if err != nil {
			l.Error("error accepting connection", "error", err)
			continue
		}
		l.Info("connection accepted", "address", conn.RemoteAddr())
		l.Debug("handling request")
		err = handleRequest(conn, l)
		if err != nil {
			l.Error("error handling request", "error", err)
		}
		conn.Close()
		if err != nil {
			l.Error("error closing connection", "error", err)
		}
	}
}

// handleRequest reads from the given connection and logs the contents to the given logger.
func handleRequest(conn net.Conn, l logging.Logger) error {
	// Make a buffer to hold incoming data.
	buf := make([]byte, logBufSize)
	for {
		// Read into the buffer.
		n, err := conn.Read(buf)
		if err != nil {
			return fmt.Errorf("could not read from connection: %w", err)
		}

		// Log lines from buffer.
		r := bytes.NewReader(buf[:n])
		scan := bufio.NewScanner(r)
		for scan.Scan() {
			line := scan.Text()
			l.Info("new remote syslog received", "log", line)
		}
		err = scan.Err()
		if err != nil {
			return err
		}
	}
}
