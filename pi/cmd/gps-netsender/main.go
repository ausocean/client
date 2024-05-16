/*
NAME
  gps-netsender - NetSender client for interfacing with GPS sensors

AUTHOR
  Jake Lane <me@jakelane.me>

LICENSE
  gps-netsender is Copyright (C) 2018 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them under
  the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with https://github.com/ausocean/client/src/master/gpl.txt.
  If not, see http://www.gnu.org/licenses.
*/

// gps-netsender is a NetSender client for retrieving data from a GPS sensor
package main

import (
	"encoding/json"
	"flag"
	"io"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/adrianmo/go-nmea"
	"github.com/jacobsa/go-serial/serial"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/smartlogger"
	"github.com/ausocean/utils/logging"
)

// GPSData stores data collected from NMEA sentences
type GPSData struct {
	Latitude         *float64   // Latitude
	Longitude        *float64   // Longitude
	LastFixTime      *time.Time // Time of last GPS fix
	Speed            *float64   // Speed in knots
	Course           *float64   // True course
	Variation        *float64   // Magnetic variation
	FixQuality       *string    // Quality of fix.
	NumSatellites    *int64     // Number of satellites in use.
	Altitude         *float64   // Altitude.
	TrueTrack        *float64   // True track made good (degrees)
	MagneticTrack    *float64   // Magnetic track made good
	GroundSpeedKnots *float64   // Ground speed in Knots
	GroundSpeedKPH   *float64   // Ground speed in km/hr
	Heading          *float64   // Heading in degrees
	True             *bool      // Heading is relative to true north
}

// parameters are variables defined on NetReceiver instances
type parameters struct {
	readInterval time.Duration // time in seconds between sending GPS data
	mode         string        // mode of device "Normal", "Paused", "Stop"
}

// gpsClient holds all netsender and client data
type gpsClient struct {
	parameters

	ns     *netsender.Sender // NetSender instance for send/receive from server
	varSum int               // checksum for last retrieved variable state
	ip     string            // comma separated list of input pins
}

var log logging.Logger
var defaultParams = parameters{
	readInterval: 10, // 10 seconds
}

const (
	progName           = "gps-netsender"
	defaultLogPath     = "/var/log/netsender"
	mimeType           = "application/json" // mime-type to send to NetReceiver
	sentenceBufferSize = 32                 // number of sentences to keep before discarding
)

func main() {
	serialPort := flag.String("SerialPort", "/dev/ttyS0", "Serial Port for GPS module")
	baudRate := flag.Uint("BaudRate", 9600, "Baud rate of GPS module")
	logLevel := flag.Int("LogLevel", int(logging.Debug), "Specifies log level")
	logPath := flag.String("LogPath", defaultLogPath, "Specifies log path")
	configFile := flag.String("ConfigFile", "", "Specifies NetSender config file")
	flag.Parse()

	validLogLevel := true
	if *logLevel < int(logging.Debug) || *logLevel > int(logging.Fatal) {
		*logLevel = int(logging.Info)
		validLogLevel = false
	}

	// Create logger
	logSender := smartlogger.New(*logPath)
	log = logging.New(int8(*logLevel), &logSender.LogRoller, true)
	log.Info( "log-netsender: Logger Initialized")
	if !validLogLevel {
		log.Error( "Invalid log level was defaulted to Info")
	}

	// Open serial port
	options := serial.OpenOptions{
		PortName:        *serialPort,
		BaudRate:        *baudRate,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 4,
	}
	port, err := serial.Open(options)
	if err != nil {
		log.Error( "serial.Open failed", "error", err.Error())
		os.Exit(1)
	}
	defer port.Close()
	log.Info( "Opened serial port")

	gc := gpsClient{
		parameters: defaultParams,
	}

	// Start NetSender
	gc.ns, err = netsender.New(log, nil, nil, nil, netsender.WithConfigFile(*configFile))
	if err != nil {
		log.Error( "netsender.New failed", "error", err.Error())
		os.Exit(1)
	}

	// Define synchronously initial vars
	v, err := gc.ns.Vars()
	if err != nil {
		log.Warning( "netsender.Vars failed", "error", err.Error())
	}
	params, _ := gc.updateVars(gc.parameters, v)
	gc.parameters = params
	gc.varSum = gc.ns.VarSum()

	// Data channels
	raw := make(chan string, sentenceBufferSize)
	data := make(chan GPSData, 1) // buffer a single data object
	vars := make(chan parameters)

	// Define initial config
	gc.reconfig()

	// Constantly update vars
	go gc.checkVars(vars)

	// Parse sentences and update local state
	go parseSentences(raw, data)

	// Send data via network
	go gc.send(data, vars)

	// Read GPS data from serial port
	gc.readGPS(port, raw)
}

// Constantly check for new vars and update if found
func (gc *gpsClient) checkVars(vars chan parameters) {
	// TODO: Crash handling
	for {
		if gc.varSum == gc.ns.VarSum() {
			continue
		}

		v, err := gc.ns.Vars()
		if err != nil {
			log.Warning( "netsender.Vars failed", "error", err.Error())
			continue
		}

		params, changed := gc.updateVars(gc.parameters, v)
		if changed {
			gc.parameters = params
			vars <- params
		}

		gc.varSum = gc.ns.VarSum()
	}
}

func (gc *gpsClient) updateVars(params parameters, vars map[string]string) (parameters, bool) {
	changed := false

	mode := vars["mode"]
	if params.mode != mode {
		params.mode = mode
		changed = true
	}

	val, ok := vars["readInterval"]
	if interval, err := strconv.Atoi(val); ok && err == nil {
		params.readInterval = time.Duration(interval)
		changed = true
	}

	return params, changed
}

func (gc *gpsClient) reconfig() {
	_, err := gc.ns.Config()
	if err != nil {
		log.Warning( "netsender.Config failed", "error", err.Error())
		return
	}
	gc.ip = gc.ns.Param("ip")
}

func processSentence(dst GPSData, raw string) GPSData {
	s, err := nmea.Parse(raw)
	if err != nil {
		log.Warning( "Failed to process sentence", "error", err.Error())
		return dst
	}

	log.Debug( "Processed sentence", "sentence", raw)

	switch s := s.(type) {
	case nmea.RMC:
		if s.Validity != "A" {
			// Not valid data
			log.Info( "Invalid data", "type", s.Prefix())
			return dst
		}

		newFix := time.Date(
			s.Date.YY,
			time.Month(s.Date.MM),
			s.Date.DD,
			s.Time.Hour,
			s.Time.Minute,
			s.Time.Second,
			s.Time.Millisecond*1000000,
			time.UTC,
		)

		// Valid, update
		dst.Latitude = &s.Latitude
		dst.Longitude = &s.Longitude
		dst.Speed = &s.Speed
		dst.Course = &s.Course
		dst.Variation = &s.Variation
		dst.LastFixTime = &newFix
	case nmea.GGA:
		currentTime := time.Now()
		newFix := time.Date(
			currentTime.Year(),
			currentTime.Month(),
			currentTime.Day(),
			s.Time.Hour,
			s.Time.Minute,
			s.Time.Second,
			s.Time.Millisecond*1000000,
			time.UTC,
		)
		dst.Latitude = &s.Latitude
		dst.Longitude = &s.Longitude
		dst.FixQuality = &s.FixQuality
		dst.NumSatellites = &s.NumSatellites
		dst.Altitude = &s.Altitude
		dst.LastFixTime = &newFix
	case nmea.GLL:
		if s.Validity != "A" {
			// Not valid data
			log.Info( "Invalid data", "type", s.Prefix())
			return dst
		}

		currentTime := time.Now()
		newFix := time.Date(
			currentTime.Year(),
			currentTime.Month(),
			currentTime.Day(),
			s.Time.Hour,
			s.Time.Minute,
			s.Time.Second,
			s.Time.Millisecond*1000000,
			time.UTC,
		)
		dst.Latitude = &s.Latitude
		dst.Longitude = &s.Longitude
		dst.LastFixTime = &newFix
	case nmea.VTG:
		dst.TrueTrack = &s.TrueTrack
		dst.MagneticTrack = &s.MagneticTrack
		dst.GroundSpeedKnots = &s.GroundSpeedKnots
		dst.GroundSpeedKPH = &s.GroundSpeedKPH
	case nmea.HDT:
		dst.Heading = &s.Heading
		dst.True = &s.True
	case nmea.ZDA, nmea.PGRME, nmea.GSV, nmea.GSA:
		log.Debug( "Sentence not implemented", "sentence", s.Prefix())
	default:
		log.Warning( "Failure to read parsed sentence", "sentence", s.String())
	}

	return dst
}

func parseSentences(raw chan string, data chan GPSData) {
	lastData := GPSData{}
	for r := range raw {
		lastData = processSentence(lastData, r)
		select {
		case data <- lastData:
		default:
			// Safely clear oldest element in chan.
			select {
			case <-data:
			default:
			}
			data <- lastData
		}
	}
}

func (gc *gpsClient) send(data chan GPSData, vars chan parameters) {
	log.Info( "Starting send worker")

	pins := netsender.MakePins(gc.ip, "T")
	params := gc.parameters
	for d := range data {
		// Update params if there are any pending
		select {
		case params = <-vars:
			// Got new vars
		default:
			// Use previous vars
		}

		msg, err := json.Marshal(d)
		if err != nil {
			log.Fatal( "Failed to generate json", "error", err.Error())
		}

		for i, pin := range pins {
			if pin.Name == "T1" {
				pins[i].Value = len(msg)
				pins[i].Data = msg
				pins[i].MimeType = mimeType
			}
		}

		_, rc, err := gc.ns.Send(netsender.RequestPoll, pins)
		if err != nil {
			log.Error( "netsender.Send failed", "error", err.Error())
		} else if rc == netsender.ResponseUpdate {
			gc.reconfig()
		}

		time.Sleep(time.Duration(params.readInterval) * time.Second)
	}
}

func (gc *gpsClient) readGPS(port io.ReadWriteCloser, raw chan string) {
	log.Info( "Starting to read from serial port")
	r := make([]byte, 32)
	var b strings.Builder
	for {
		n, err := port.Read(r)
		if err != nil {
			log.Warning( "Error reading from serial port", "error", err.Error())
		}
		if n > 0 {
			r = r[:n]
			processBuffer(r, &b, raw)
		}
	}
}

func processBuffer(r []byte, b *strings.Builder, raw chan string) {
	for _, c := range r {
		if c == '\r' {
			// Ignore CR
			continue
		}

		if c == '\n' {
			// End of line, completed a sentence
			line := b.String()

			select {
			case raw <- line:
			default:
				// Safely clear oldest element in chan.
				select {
				case <-raw:
				default:
				}
				raw <- line
				log.Warning( "Dropped a sentence")
			}

			// Reset buffer and continue
			b.Reset()
			continue
		}

		// Write byte to buffer
		b.WriteByte(c)
	}
}
