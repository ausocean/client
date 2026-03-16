/*
  Name:
    Remote ESP Tool - CLI tool for interacting with AusOcean ESP devices
    over the network.

  Authors:
    David Sutton <davidsutton@ausocean.org>

  License:
    Copyright (C) 2026 The Australian Ocean Lab (AusOcean).

    This file is part of NetSender. NetSender is free software: you can
    redistribute it and/or modify it under the terms of the GNU
    General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option)
    any later version.

    NetSender is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NetSender in gpl.txt.  If not, see
    <http://www.gnu.org/licenses/>.
*/

package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"os/signal"
	"reflect"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/charmbracelet/lipgloss"
	"github.com/charmbracelet/log"
)

const version = "0.1.0"

// Network Settings.
const (
	udpNetwork = "udp"
	address    = "0.0.0.0"
	port       = 4040
)

// Colours.
const (
	blue = "#156eb3"
	gold = "#fdb11c"
)

// printBanner uses lipgloss to create a branded CLI header.
func printBanner() {
	// Outer box style with Blue border.
	boxStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color(blue)).
		Padding(0, 3).
		Align(lipgloss.Center)

	// Text styles.
	titleStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color(gold)).
		Bold(true)

	metaStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("#888888")).
		Faint(true)

	// Render the text.
	title := titleStyle.Render("Remote ESP Tool")
	meta := metaStyle.Render("AusOcean  •  GPL-2.0 License")

	// Stack and print.
	content := lipgloss.JoinVertical(lipgloss.Center, title, meta)
	fmt.Println(boxStyle.Render(content))
	fmt.Println()
}

func main() {
	log.SetLevel(log.DebugLevel)

	printBanner()
	log.Info("✅ Starting...", "version", version)

	fullAddr := fmt.Sprintf("%s:%d", address, port)
	conn := getUDPConnection(fullAddr)
	log.Info("✅ Network bound", "protocol", udpNetwork, "address", fullAddr)

	logFile := createLogFile()
	defer logFile.Close()
	log.Info("✅ File logger initialized", "file", logFile.Name())

	log.Info("✅ Ready and listening for incoming logs...")
	fmt.Println(strings.Repeat("━", 55))

	ch := make(chan string, 32)
	go readLogs(ch, conn)
	go fanOut(ch, toStdout, toFile(logFile))

	// Graceful Shutdown Logic.
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Block the main thread until a signal is received.
	<-sigChan

	// Clean up resources before exiting.
	fmt.Println("\r")
	log.Warn("Shutdown signal received. Cleaning up...")
	conn.Close()
	logFile.Close()
	log.Info("Graceful shutdown complete.")
}

// getUDPConnection will return a UDP connection, or cause a fatal error.
func getUDPConnection(addr string) *net.UDPConn {
	udpAddr, err := net.ResolveUDPAddr(udpNetwork, addr)
	if err != nil {
		log.Fatalf("unable to resolve UDP address: %v", err)
	}
	udpConn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		log.Fatalf("unable to listen to UDP: %v", err)
	}

	return udpConn
}

// createLogFile creates a timestamped log file.
func createLogFile() *os.File {
	filename := time.Now().Format("060102-15:04.log")
	logFile, err := os.Create(filename)
	if err != nil {
		log.Fatal("unable to open logfile", "error", err)
	}

	return logFile
}

// readLogs takes a UDP connection and scans for lines, writing them to the chan.
func readLogs(ch chan string, conn *net.UDPConn) {
	s := bufio.NewScanner(conn)

	for {
		if !s.Scan() {
			time.Sleep(100 * time.Millisecond)
		}

		ch <- s.Text()
	}
}

// fanOut takes a channel and copies the incoming strings into
// the passed list of output functions.
func fanOut(ch chan string, outputs ...func(string) error) {
	for line := range ch {
		for _, o := range outputs {
			err := o(line)
			if err != nil {
				funcPointer := reflect.ValueOf(o).Pointer()
				funcName := runtime.FuncForPC(funcPointer).Name()
				log.Error("unable to call output function", "function", funcName, "error", err)
			}
		}
	}
}

func toStdout(log string) error {
	fmt.Println(log)
	return nil
}

func toFile(file *os.File) func(string) error {
	return func(line string) error {
		_, err := fmt.Fprintf(file, "%s\n", line)
		return err
	}
}
