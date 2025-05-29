/*
NAME
  netsender - common package for NetSender clients.

AUTHORS
  Jack Richardson <richardson.jack@outlook.com>
  Alan Noble <alan@ausocean.org>
  Dan Kortschak <dan@ausocean.org>
  Saxon Nelson-Milton <saxon@ausocean.org>

LICENSE
  netsender is Copyright (C) 2017-2024 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License in
  gpl.txt. If not, see http://www.gnu.org/licenses.
*/

// Package netsender implements common functionality for "NetReceiver"
// clients, which are known as "NetSenders".
package netsender

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"math/rand"
	"net"
	"net/http"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/ausocean/utils/filemap"
	"github.com/ausocean/utils/sliceutils"
)

// Service requests types.
const (
	RequestDefault = iota
	RequestConfig
	RequestPoll
	RequestAct
	RequestVars
	RequestMts
)

// Service response codes.
const (
	ResponseNone = iota - 1
	ResponseOK
	ResponseUpdate
	ResponseReboot
	ResponseDebug
	ResponseUpgrade
	ResponseAlarm
	ResponseTest
	ResponseShutdown
)

// Pin directions, used by PinInit.
const (
	PinIn = iota
	PinOut
)

// Net speed testing consts.
const (
	downloadTestPath = "/api/test/download/"
	downloadTestSize = 1250000 // 10 megabits
	downloadTestPin  = "X1"
	uploadTestPath   = "/api/test/upload/"
	uploadRandSeed   = 845681267
	uploadTestSize   = downloadTestSize
	uploadTestPin    = "X2"
)

// Logger is the interface NetSender expects clients to use for logging.
type Logger interface {
	// SetLevel sets the level of the Logger. Calls to Log with a
	// level above the set level will be logged, all others will
	// be omitted from logging.
	SetLevel(int8)

	// Log requests the Logger to write a log entry at the given
	// level with a defined message and a set of message
	// parameters.
	Log(level int8, message string, params ...interface{})
}

// Log levels. The use of these levels is implementation dependent,
// however NetSender requires at least DebugLevel, InfoLevel,
// WarningLevel and ErrorLevel to be implemented. NetSender does not
// use FatalError, but this is included for completeness.
// NB: Levels have the same value as their zap counterparts,
// although not all of the zap levels are used.
const (
	DebugLevel   int8 = -1
	InfoLevel    int8 = 0
	WarningLevel int8 = 1
	ErrorLevel   int8 = 2
	FatalLevel   int8 = 5
)

// NetSender log messages.
const (
	errorConfigWrite      = "error writing config"
	warnPinRead           = "error reading pin"
	warnPinWrite          = "error writing pin"
	warnHttpError         = "http error"
	warnHttpResponse      = "error in response"
	warnSetLogLevel       = "unsupported log level"
	warnRebootError       = "error rebooting"
	warnMissingDeviceType = "device type required for upgrade"
	warnUpgraderNotFound  = "upgrader not found"
	warnUpgraderError     = "error executing upgrader"
	warnUpgradeFailed     = "upgrade failed"
	infoConfig            = "received config"
	infoConfigParams      = "config params"
	infoConfigParamChange = "config param changed"
	infoUpdateRequest     = "received update request"
	infoRebootRequest     = "received reboot request"
	infoDebugRequest      = "received debug request"
	infoUpgradeRequest    = "received upgrade request"
	infoTestRequest       = "received test request"
	infoShutdownRequest   = "received shutdown request"
	infoRebooting         = "rebooting"
	infoStackTrace        = "stack trace"
	infoReceivedVars      = "received vars"
	infoUpdateRequired    = "update required"
	infoUpgrading         = "upgrade in progress"
	infoUpgraded          = "completed upgrade"
	debugRunning          = "running"
	debugSendStackTrace   = "sending stack trace"
	debugSleeping         = "sleeping"
	debugHttpRequest      = "http request"
	debugHttpReply        = "http reply"
	debugVarsumChanged    = "varsum changed"
	debugConfigWrite      = "wrote config"
	debugSetLogLevel      = "set log level"
)

// NetSender modes and errors.
const (
	modeCompleted = "Completed"
	errorUpgrade  = "upgradeError"
)

var errNoKey = errors.New("key not found in JSON")

// Sender represents state for a NetSender client.
type Sender struct {
	logger     Logger            // Our logger.
	mu         sync.Mutex        // Protects our state.
	configFile string            // Path to config file.
	config     map[string]string // Our latest configuration.
	services   map[string]string // Services we use.
	configured bool              // True if we're configured, false otherwse.
	varSum     int               // Most recent var sum received from the service.
	mode       string            // Client mode.
	error      string            // Client error string, if any.
	sync       bool              // True if we need to sync client mode or error with the service, false otherwise.
	init       PinInit           // Pin initialization function, or nil.
	read       PinReadWrite      // Pin read function, or nil.
	write      PinReadWrite      // Pin write function, or nil.
	configPins []Pin             // Pins sent in the config request.
	upgrader   string            // Upgrader command.
	upgrading  bool              // True if upgrading, false otherwise.
	upload     int               // Measured upload speed in bits per second (in test mode).
	download   int               // Measured download speed in bits per second (in test mode).
}

// PinInit defines a pin initialization function, which takes a Pin and arbitrary intialization data.
// This can be used to initialize hardware pins, etc.
type PinInit func(pin *Pin, data interface{}) error

// PinReadWrite either reads or writes a Pin.
// When used as a reader, pin.Value is updated with the read value.
// When reading binary data, pin.Data and pin.MimeType should also be set, otherwise pin.Data should be nil.
// When used as a writer, pin.Value, etc. is supplied by the caller.
type PinReadWrite func(pin *Pin) error

// local consts
const (
	pkgName         = "netsender"
	version         = 173
	defaultService  = "data.cloudblue.org"
	monPeriod       = 60
	rebooter        = "syncreboot"
	defaultLogLevel = WarningLevel
	stackTraceSize  = 1 << 16
)

// ServerError represents service error codes.
type ServerError struct {
	er string
}

func (e *ServerError) Error() string {
	return e.er
}

const (
	defaultConfigFile = "/etc/netsender.conf" // Default config file. Customize with WithConfigFile.
	defaultUpgrader   = "pkg-upgrade.sh"      // Default upgrade script. Customize with WithUpgrader
)

// Timeout is the timeout used for network calls.
var Timeout = 20 * time.Second

// rebootTime is the time we rebooted, which we use to calculate
// uptime. If we are not networked at the time it will be a fake time
// since the Pi does not have a real-time clock, but since we only
// care about differences that doesn't matter.
var rebootTime = time.Now()

// Pseudo consts (since Go doesn't allow const string arrays).
// ma: MAC address
// dk: device key
// wi: wifi SSID,password
// ip: input pins
// op: output pins
// mp: monitor period
// ap: act period
// ct: client type
// cv: client version
// hw: hardware
// sh: service host
// configParams specifies accepted parameters and the order in which they are written to ConfigFile.
// configNumbers specifies configuration parameters which have numeric values.
// requestTypes specifies service request types.
// NB: hw and sh are client-side config params (i.e., not stored by the service).
var (
	configParams  = []string{"ma", "dk", "wi", "ip", "op", "mp", "ap", "ct", "cv", "hw", "sh"}
	configNumbers = []string{"dk", "mp", "ap"}
	requestTypes  = []string{"default", "config", "poll", "act", "vars", "mts"}
)

// New returns a pointer to newly instantiated and intialized Netsender instance
func New(logger Logger, init PinInit, read, write PinReadWrite, options ...Option) (*Sender, error) {
	var ns Sender
	err := ns.Init(logger, init, read, write, options...)
	return &ns, err
}

// Init initializes the NetSender client by reading cached configuration data
// and initializing data that is sent with config requests.
func (ns *Sender) Init(logger Logger, init PinInit, read, write PinReadWrite, options ...Option) error {
	ns.logger = logger
	ns.configFile = defaultConfigFile
	ns.upgrader = defaultUpgrader
	// Set download upload speeds to -1 to indicate they have not been deduced yet.
	ns.upload, ns.download = -1, -1
	ns.init, ns.read, ns.write = init, read, write
	err := ns.initPins()
	if err != nil {
		return err
	}

	for i, o := range options {
		if o == nil {
			return errors.New("cannot apply nil option")
		}
		err := o(ns)
		if err != nil {
			return fmt.Errorf("could not apply option no. %d, %w", i, err)
		}
	}

	config, err := ns.readConfig()
	if err != nil {
		return err
	}

	services, err := configServices(config["sh"])
	if err != nil {
		return err
	}

	ns.mu.Lock()
	ns.config = config
	ns.services = services
	var params []interface{}
	for _, name := range configParams {
		params = append(params, name, ns.config[name])
	}
	ns.logger.Log(InfoLevel, infoConfigParams, params...)
	ns.mu.Unlock()

	return nil
}

// initPins initializes all pins, if any
func (ns *Sender) initPins() error {
	if ns.init == nil {
		return nil
	}
	for _, pin := range MakePins(ns.Param("ip"), "") {
		if err := ns.init(&pin, PinIn); err != nil {
			return err
		}
	}
	for _, pin := range MakePins(ns.Param("op"), "") {
		if err := ns.init(&pin, PinOut); err != nil {
			return err
		}
	}
	return nil
}

// Run sends requests to the service and handles responses.
// Clients are responsible for calling Run regularly.
// Clients are responsible for handling variable changes separately.
func (ns *Sender) Run() error {
	ns.logger.Log(DebugLevel, debugRunning)

	rc := ResponseNone
	var err error
	var sent bool
	var reply string

	ip := ns.Param("ip")
	op := ns.Param("op")
	outputs := MakePins(op, "")
	if ip != "" {
		inputs := MakePins(ip, "")
		if ns.read != nil {
			for i := range inputs {
				// If download and upload speed pins are specified, set.
				// NB: ns.download and ns.upload are set to -1 in ns.Init,
				// i.e. this will indicate speeds have not yet been calculated.
				switch inputs[i].Name {
				case downloadTestPin:
					inputs[i].Value = ns.download
				case uploadTestPin:
					inputs[i].Value = ns.upload
				}

				err := ns.read(&inputs[i])
				if err != nil {
					ns.logger.Log(WarningLevel, warnPinRead, "error", err.Error(), "pin", inputs[i].Name)
				}
			}
		}

		reply, rc, err = ns.Send(RequestPoll, inputs)
		if err != nil {
			return err
		}
		sent = true

	} else if op != "" {
		reply, rc, err = ns.Send(RequestAct, outputs)
		if err != nil {
			return err
		}
		sent = true
	}
	if sent && op != "" {
		dec, err := NewJSONDecoder(reply)
		if err != nil {
			return fmt.Errorf("JSON decoding error: %w", err)
		}
		if ns.write != nil {
			for _, pin := range outputs {
				v, err := dec.Int(pin.Name)
				if err != nil {
					return fmt.Errorf("cannot decode pin value: %w", err)
				}
				pin.Value = v
				ns.logger.Log(DebugLevel, fmt.Sprintf("writing value %d to pin %s", v, pin.Name))
				err = ns.write(&pin)
				if err != nil {
					ns.logger.Log(WarningLevel, warnPinWrite, "error", err.Error(), "pin", pin.Name)
				}
			}
		}
	}
	if !sent {
		rc, err = ns.Config()
		if err != nil {
			return err
		}
	}

	// Handle the service response, if any.
	switch rc {
	case ResponseUpdate:
		ns.logger.Log(InfoLevel, infoUpdateRequest)
		_, err = ns.Config()
		return err

	case ResponseReboot:
		ns.logger.Log(InfoLevel, infoRebootRequest)
		if !ns.IsConfigured() {
			ns.logger.Log(InfoLevel, infoUpdateRequired)
			return nil
		}
		ns.logger.Log(InfoLevel, infoRebooting)
		err := exec.Command(rebooter).Run()
		if err != nil {
			ns.logger.Log(WarningLevel, warnRebootError)
			return err
		}

	case ResponseShutdown:
		ns.logger.Log(InfoLevel, infoShutdownRequest)
		if !ns.IsConfigured() {
			ns.logger.Log(DebugLevel, "need to config for shutdown request")
			_, err := ns.Config()
			if err != nil {
				return fmt.Errorf("could not perform config request for shutdown request: %w", err)
			}
		}
		out, err := exec.Command("syncreboot", "-s=true").CombinedOutput()
		if err != nil {
			return fmt.Errorf("could not use syncreboot to shutdown, out: %s, err: %w", string(out), err)
		}

	case ResponseDebug:
		ns.logger.Log(InfoLevel, infoDebugRequest)
		if !ns.IsConfigured() {
			ns.logger.Log(InfoLevel, infoUpdateRequired)
			return nil
		}
		return ns.Debug()

	case ResponseUpgrade:
		ns.logger.Log(InfoLevel, infoUpgradeRequest)
		if ns.Mode() == modeCompleted {
			return nil // Nothing to do.
		}
		if ns.IsUpgrading() {
			ns.logger.Log(InfoLevel, infoUpgrading)
			return nil
		}

		// Perform the upgrade concurrently.
		go ns.Upgrade()

	case ResponseTest:
		ns.logger.Log(InfoLevel, infoTestRequest)
		err := ns.TestDownload()
		if err != nil {
			return fmt.Errorf("could not test download speed: %w", err)
		}

		err = ns.TestUpload()
		if err != nil {
			return fmt.Errorf("could not test upload speed: %w", err)
		}
	}
	return nil
}

// TestDownload estimates net download speed by downloading a file using the
// /api/test/download/ request and timing how long it takes. The calculated
// speed is stored in ns.download, from which we can set the X0 pin if specified in
// the netsender config.
func (ns *Sender) TestDownload() error {
	ns.logger.Log(InfoLevel, "testing download")
	url := "http://" + ns.services["default"] + downloadTestPath + strconv.Itoa(downloadTestSize)

	// Download test data and time how long it takes.
	now := time.Now()
	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("could not do download speed test request: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		resp.Body.Close()
		return fmt.Errorf("download test request response status is %d and not 200 OK", resp.StatusCode)
	}
	body, err := io.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		return fmt.Errorf("download test failed to read body: %v", err)
	}
	if len(body) != downloadTestSize {
		return fmt.Errorf("download test expected %d bytes, got %d bytes", downloadTestSize, len(body))
	}
	dur := time.Now().Sub(now).Seconds()

	// Calculate download speed in bits/s.
	ns.download = int((downloadTestSize * 8) / dur)
	ns.logger.Log(InfoLevel, "determined download speed", "speed(bits/s)", ns.download)
	return nil
}

// TestUpload estimates net upload speed by uploading randomly
// generated bytes using the /api/test/upload/ request and timing how
// long it takes. The calculated speed is stored in ns.upload, from
// which we can set the X1 pin if specified in the netsender config.
func (ns *Sender) TestUpload() error {
	ns.logger.Log(InfoLevel, "testing upload")
	url := "http://" + ns.services["default"] + uploadTestPath + strconv.Itoa(uploadTestSize)

	// Create upload data.
	rand.Seed(uploadRandSeed)
	body := make([]byte, uploadTestSize)
	rand.Read(body)

	// Upload test data and time how long it takes.
	now := time.Now()
	resp, err := http.Post(url, "application/octet-stream", bytes.NewBuffer(body))
	if err != nil {
		return fmt.Errorf("could not upload test data: %w", err)
	}
	dur := time.Now().Sub(now).Seconds()
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("upload test request response status is %d and not 200 OK", resp.StatusCode)
	}

	// Calculate upload speed in bits/s.
	ns.upload = int((uploadTestSize * 8) / dur)

	ns.logger.Log(InfoLevel, "determined upload speed", "speed(bits/s)", ns.upload)
	return nil
}

type SendOption func(ns *Sender) error

// WithMtsAddress sets the HTTP address for the mts type to addr.
func WithMtsAddress(addr string) SendOption {
	return func(ns *Sender) error {
		ns.services["mts"] = addr
		return nil
	}
}

// WithRecvAddress is for backwards compatibility and is equivalent to WithMtsAddress.
func WithRecvAddress(addr string) SendOption {
	return WithMtsAddress(addr)
}

// Send invokes an HTTP request for the specified requestType, returning:
//
//	reply: the service response in JSON format.
//	rc: the service response code.
//	error: a network error, if any.
//
// Pin values must be pre-populated by the caller.
// See http://netreceiver.appspot.com/help#protocol for a description
// of the service requests.
func (ns *Sender) Send(requestType int, pins []Pin, opts ...SendOption) (reply string, rc int, err error) {
	for i, opt := range opts {
		err := opt(ns)
		if err != nil {
			return "", 0, fmt.Errorf("could not apply option no. %d: %v", i, err)
		}
	}
	var path string
	var uptime = int(time.Since(rebootTime).Seconds())
	rc = ResponseNone

	switch requestType {
	case RequestPoll, RequestMts, RequestAct, RequestConfig, RequestVars:
		path = fmt.Sprintf("/%s?vn=%d&ma=%s&dk=%s&ut=%d", requestTypes[requestType], version, ns.Param("ma"), ns.Param("dk"), uptime)

	default:
		return reply, rc, errors.New("Invalid request type: " + strconv.Itoa(requestType))
	}

	ns.mu.Lock()
	if ns.sync {
		// Sync the mode and (optionally) error with the service.
		path += "&md=" + ns.mode
		if ns.error != "" {
			path += "&er=" + ns.error
		}
	}
	ns.mu.Unlock()

	// Append pin parameters to URL path.
	for _, pin := range pins {
		if !hasValidData(pin) {
			continue
		}
		path += "&" + pin.Name + "="
		if pin.MimeType != "" || len(pin.Data) == 0 {
			path += strconv.Itoa(pin.Value)
		} else {
			path += string(pin.Data)
		}
	}

	// Look up the service host to use for this requestType, else use the default host.
	host := ns.services[requestTypes[requestType]]
	if host == "" {
		host = ns.services["default"]
	}

	ns.logger.Log(DebugLevel, debugHttpRequest, "host", host, "request", path)
	reply, err = httpRequest(host, path, pins)
	if err != nil {
		ns.logger.Log(WarningLevel, warnHttpError, "error", err.Error())
		return reply, rc, err
	}

	ns.logger.Log(DebugLevel, debugHttpReply, "reply", reply)
	if !strings.HasPrefix(reply, "{") {
		return reply, rc, errors.New("Expected JSON")
	}
	dec, err := NewJSONDecoder(reply)
	if err != nil {
		return reply, rc, fmt.Errorf("error decoding reply: %v, error: %w", reply, err)
	}
	er, err := dec.String("er")
	if err == nil {
		ns.logger.Log(WarningLevel, warnHttpResponse, "er", er)
		return reply, rc, &ServerError{er: er}
	}
	rc, err = dec.Int("rc")
	if err != nil {
		rc = ResponseOK
	}

	// We don't expect a varsum in response to mts requests.
	if requestType == RequestMts {
		return reply, rc, nil
	}

	// Extract and check the var sum if it exists in the response.
	// It must exist and must be a string if the response is from a vars request,
	// for any other type of request it should exist as an integer or not at all.
	var vs int
	if requestType == RequestVars {
		val, err := dec.String("vs")
		if err != nil {
			return reply, rc, fmt.Errorf("vs missing: %w", err)
		}
		vs, err = strconv.Atoi(val)
		if err != nil {
			return reply, rc, fmt.Errorf("vs not an integer: %w", err)
		}
	} else {
		vs, err = dec.Int("vs")
		if err == errNoKey {
			return reply, rc, nil
		} else if err != nil {
			return reply, rc, fmt.Errorf("invalid vs: %w", err)
		}
	}
	ns.mu.Lock()
	if vs != ns.varSum {
		ns.logger.Log(DebugLevel, debugVarsumChanged)
	}
	ns.varSum = vs
	ns.mu.Unlock()

	return reply, rc, nil
}

// hasValidData checks a pin for data to be sent.
func hasValidData(p Pin) bool {
	return p.Value != -1 && (p.MimeType == "" || len(p.Data) != 0)
}

// localAddr returns the preferred local IP address as a string.
func localAddr() string {
	if conn, err := net.Dial("udp", "8.8.8.8:80"); err == nil {
		// NB: dialing a UDP connection does not actually create a connection
		defer conn.Close()
		str := conn.LocalAddr().String()
		if port := strings.Index(str, ":"); port > 0 {
			return str[:port] // strip port number
		}
		return str
	}
	return ""
}

// httpRequest invokes an HTTP request.
// GET is used when pins contain no payload data, POST otherwise.
func httpRequest(address, path string, pins []Pin) (string, error) {
	method := "GET"
	var ior io.Reader
	var pr *PayloadReader
	var sz int
	var mt string
	if pins != nil {
		var sendPins []Pin
		for _, pin := range pins {
			if len(pin.Data) == 0 || pin.MimeType == "" {
				continue
			}
			if len(pin.Data) != pin.Value {
				return "", errors.New("Pin Data length does not match Value")
			}
			sz += pin.Value
			sendPins = append(sendPins, pin)
			mt = pin.MimeType
			if method == "GET" {
				method = "POST"
			}
		}
		pr = NewPayloadReader(sendPins)
		ior = pr
	}

	req, err := http.NewRequest(method, "http://"+address+path, ior)
	if err != nil {
		return "", err
	}
	if pr != nil {
		req.ContentLength = int64(pr.Len())
		snapshot := *pr
		req.GetBody = func() (io.ReadCloser, error) {
			r := snapshot
			return ioutil.NopCloser(&r), nil
		}
	}
	if method == "POST" {
		req.Header.Set("Content-Length", strconv.Itoa(sz))
		req.Header.Set("Content-Type", mt)
	}

	client := &http.Client{Timeout: Timeout, Transport: http.DefaultTransport}
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	// Return the last line of the response, unless we encounter an error.
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	bodyLines := strings.Split(string(body), "\n")

	if resp.Status == "200 OK" {
		return bodyLines[len(bodyLines)-1], nil
	} else {
		return "", errors.New("Response was not 200 OK")
	}
}

// Config requests configuration information from the service via a /config request.
// A config request can result in a new request from the service, which is returned as rc.
// Config parameters that have changed are updated.
// Missing or invalid config parameters are silently ignored.
func (ns *Sender) Config() (rc int, err error) {
	ns.mu.Lock()
	ns.configured = false
	ns.mu.Unlock()

	var reply string
	if reply, rc, err = ns.Send(RequestConfig, ns.configPins); err != nil {
		return rc, err
	}

	ns.logger.Log(InfoLevel, infoConfig, "config", reply)
	dec, err := NewJSONDecoder(reply)
	if err != nil {
		return rc, err
	}

	changed := false
	ns.mu.Lock()
	for _, name := range configParams {
		var num int
		var val string
		if sliceutils.ContainsString(configNumbers, name) {
			if num, err = dec.Int(name); err == nil {
				val = strconv.Itoa(num)
			}
		} else {
			val, err = dec.String(name)
		}
		if err != nil {
			continue
		}
		if val != ns.config[name] {
			ns.config[name] = val
			ns.logger.Log(InfoLevel, infoConfigParamChange, "name", name, "value", val)
			changed = true
		}
	}
	ns.configured = true
	ns.mu.Unlock()

	if changed {
		ns.mu.Lock()
		err := ns.writeConfig(ns.config)
		if err == nil {
			ns.logger.Log(DebugLevel, debugConfigWrite)
		} else {
			ns.logger.Log(ErrorLevel, errorConfigWrite, "error", err.Error())
		}
		ns.mu.Unlock()
		ns.initPins()
	}
	return rc, nil
}

// Debug logs a stack trace. If T0 (log text) is present as an input,
// the stack trace is sent to the service, followed by a config
// request.
func (ns *Sender) Debug() error {
	buf := make([]byte, stackTraceSize)
	size := runtime.Stack(buf, true)
	ns.logger.Log(InfoLevel, infoStackTrace, "stack", string(buf[:size]))

	if !strings.Contains(ns.Param("ip"), "T0") {
		return nil
	}

	ns.logger.Log(DebugLevel, debugSendStackTrace)
	pin := Pin{Name: "T0", Value: size, Data: buf[:size], MimeType: "text/plain"}
	_, _, err := ns.Send(RequestPoll, []Pin{pin})
	if err != nil {
		return err
	}
	_, err = ns.Config()
	return err
}

// IsConfigured returns true if (1) NetSender has been configured the first time or
// (2) configured since the most recent update request.
func (ns *Sender) IsConfigured() bool {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.configured
}

// IsUpgrading returns true if an upgrade is is progress.
func (ns *Sender) IsUpgrading() bool {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.upgrading
}

// Param returns a single config parameter value.
func (ns *Sender) Param(param string) string {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.config[param]
}

// VarSum gets the currently cached var sum, i.e., the last var sum received from the service.
// The var sum is a 32-bit CRC-style checksum of the current the service variables and their values.
func (ns *Sender) VarSum() int {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.varSum
}

// Vars requests the current variables from the service via a /vars request.
// Also updates and returns the current var sum.
// Special vars:
//
//	id: the ID assigned to this device by the service (always present).
//	mode: device-specific operating mode of this device, defaults to "Normal".
//	logging: the log level, one of "Error", "Warn", "Info", or "Debug"
//	vs: the var sum (in _string_ form)
func (ns *Sender) Vars() (map[string]string, error) {
	var reply string
	var err error
	var vars map[string]string

	if reply, _, err = ns.Send(RequestVars, nil); err != nil {
		return vars, err
	}
	ns.logger.Log(InfoLevel, infoReceivedVars, "vars", reply)

	decoder := json.NewDecoder(strings.NewReader(reply))
	if err := decoder.Decode(&vars); err != nil {
		return vars, err
	}

	er, present := vars["er"]
	if present {
		return vars, errors.New(er)
	}

	id, present := vars["id"]
	if present {
		for key, value := range vars {
			if strings.HasPrefix(key, id+".") {
				vars[key[len(id)+1:]] = value
				delete(vars, key)
			}
		}
	}

	// Check for special vars.
	_, present = vars["mode"]
	if !present {
		vars["mode"] = "Normal"
	}
	_, present = vars["error"]
	if !present {
		vars["error"] = ""
	}

	// Save the mode and error.
	ns.mu.Lock()
	if ns.mode == vars["mode"] && ns.error == vars["error"] {
		ns.sync = false // client is in sync
	} else {
		ns.mode = vars["mode"]
		ns.error = vars["error"]
	}
	ns.mu.Unlock()

	logging, present := vars["logging"]
	if present {
		switch logging {
		case "Fatal":
			ns.logger.SetLevel(FatalLevel)
		case "Error":
			ns.logger.SetLevel(ErrorLevel)
		case "Warning":
			ns.logger.SetLevel(WarningLevel)
		case "Info":
			ns.logger.SetLevel(InfoLevel)
		case "Debug":
			ns.logger.SetLevel(DebugLevel)
		default:
			ns.logger.Log(WarningLevel, warnSetLogLevel, "LogLevel", logging)
			return vars, nil
		}
		ns.logger.Log(DebugLevel, debugSetLogLevel, "LogLevel", logging)
	}

	return vars, nil
}

// Mode gets the client mode value.
func (ns *Sender) Mode() string {
	ns.mu.Lock()
	mode := ns.mode
	ns.mu.Unlock()
	return mode
}

// SetMode sets the client mode, resets the client's varsum and forces a sync.
func (ns *Sender) SetMode(mode string) {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	if mode == ns.mode {
		return
	}
	ns.mode = mode
	ns.sync = true
	ns.varSum = -1
}

// Error gets the client error value.
func (ns *Sender) Error() string {
	ns.mu.Lock()
	error := ns.error
	ns.mu.Unlock()
	return error
}

// SetError sets the client error, resets the client's varsum and forces a sync.
func (ns *Sender) SetError(error string) {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	if error == ns.error {
		return
	}
	ns.error = error
	ns.sync = true
	ns.varSum = -1
}

// Upgrade performs an upgrade of the device software for the
// configured client type (ct) and client version (cv). Sets the mode
// to modeCompleted upon completion, successful or otherwise. Sets the
// error to errorUpgrade if the upgrade fails.
func (ns *Sender) Upgrade() {
	ct := strings.ToLower(ns.Param("ct"))
	if ct == "" {
		ns.logger.Log(WarningLevel, warnMissingDeviceType)
		return
	}
	cv := strings.ToLower(ns.Param("cv"))
	if cv == "" {
		cv = "@latest"
	}

	ns.mu.Lock()
	ns.upgrading = true
	ns.mu.Unlock()

	ns.logger.Log(InfoLevel, "upgrading", "upgrader", ns.upgrader, "ct", ct, "cv", cv)
	cmd := exec.Command(ns.upgrader, ct, cv)
	err := cmd.Run()

	ns.mu.Lock()
	ns.upgrading = false
	ns.mu.Unlock()

	if err != nil {
		ns.logger.Log(WarningLevel, warnUpgraderError, "upgrader", ns.upgrader, "ct", ct, "cv", cv)
		ns.SetError(errorUpgrade)
	} else {
		ns.logger.Log(InfoLevel, infoUpgraded, "upgrader", ns.upgrader, "ct", ct, "cv", cv)
	}

	ns.SetMode(modeCompleted)
	ns.Config()
}

// writeConfig writes configuration info to configFile in configParams order.
func (s *Sender) writeConfig(config map[string]string) error {
	s.logger.Log(InfoLevel, "writing config", "config", config)
	return filemap.WriteTo(s.configFile, "\n", " ", config, configParams)
}

// readConfig reads configuration info from configFile and returns it as a map of parameter name/value pairs.
// An error is returned if required configuration parameters (ma or dk) are missing.
// Default values are supplied for other parameters that are missing.
func (s *Sender) readConfig() (map[string]string, error) {
	config, err := filemap.ReadFrom(s.configFile, "\n", " ")
	if err != nil {
		return nil, err
	}

	for _, name := range configParams {
		val, present := config[name]
		if !present {
			switch name {
			case "ma":
				return nil, errors.New("Required ma param is missing")
			case "dk":
				return nil, errors.New("Required dk param is missing")
			case "mp":
				config["mp"] = strconv.Itoa(monPeriod)
			case "ap":
				config["ap"] = "0"
			case "sh":
				config["sh"] = "default=" + defaultService
			default:
				config[name] = ""
			}
			continue
		}
		if sliceutils.ContainsString(configNumbers, name) {
			if _, err := strconv.ParseInt(val, 10, 64); err != nil {
				return nil, errors.New("Expected int for config param: " + name)
			}
		}
	}

	return config, nil
}

// configServices takes a service host (sh) parameter and returns a
// map in which keys represent the different request types and values
// represent the corresponding service host. If a single host is
// specified a map with a single entry {"default":sh} is returned.
func configServices(sh string) (map[string]string, error) {
	if !strings.ContainsAny(sh, "=,") {
		// No comma-separated values, so the one-and-only host is the default.
		return map[string]string{"default": sh}, nil
	}

	svc := filemap.Split(sh, ",", "=")
	_, ok := svc["default"]
	if !ok {
		return nil, errors.New("Missing default from sh param.")
	}
	// Check that all keys are valid request types or default.
	for k := range svc {
		if !sliceutils.ContainsString(requestTypes, k) {
			return nil, errors.New("Invalid request type: " + k)
		}
	}
	return svc, nil
}

// PayloadReader implements an io.Reader for Pin payload data.
type PayloadReader struct {
	pins []Pin
	cur  int // current pin we're reading from
	off  int // offset into the current pin
}

// NewPayloadReader returns a pointer to a newly initialized PayloadReader.
func NewPayloadReader(pins []Pin) *PayloadReader {
	return &PayloadReader{pins: pins}
}

// Len returns the remaining number of bytes to be read from the payload.
func (pr *PayloadReader) Len() int {
	var n int
	for _, d := range pr.pins[pr.cur:] {
		n += len(d.Data)
	}
	n -= pr.off
	return n
}

// Read reads the next len(b) bytes from the payload or until the payload is drained.
// The return value n is the number of bytes read. If the payload has no data to
// return and len(b) is not zero, err is io.EOF, otherwise nil.
func (pr *PayloadReader) Read(b []byte) (int, error) {
	if len(b) == 0 {
		return 0, nil
	}
	var n int
	for pr.cur < len(pr.pins) {
		pd := pr.pins[pr.cur].Data[pr.off:]
		if len(pd) == 0 {
			pr.cur++
			pr.off = 0
			continue
		}
		_n := copy(b[n:], pd)
		if _n == 0 {
			break
		}
		pr.off += _n
		n += _n
	}
	if n < len(b) {
		return n, io.EOF
	}
	return n, nil
}

// Pin contains a pin name, its integer value and an optional payload data.
// A "pin" may refer to a physical pin on the device, such as an
// analog (A) or digital (D) pin or a software-defined sensor (X for a
// scalar, B for binary data, V for video, etc.)
type Pin struct {
	Name     string
	Value    int
	Data     []byte
	MimeType string
}

// MakePins makes a Pin array from a CSV-separated string of pin names,
// optionally restricting to pins of a certain type.
// Values are -1 by default.
func MakePins(csv string, restrict string) []Pin {
	var pins []Pin
	if len(csv) == 0 {
		return pins
	}
	all := strings.Split(csv, ",")
	var included []string
	if restrict != "" {
		included = strings.Split(restrict, ",")
	}
	pins = make([]Pin, 0, len(all))
	for _, pin := range all {
		if len(pin) == 0 {
			continue
		}
		if restrict == "" || sliceutils.ContainsString(included, pin[:1]) {
			pins = append(pins, Pin{Name: pin, Value: -1})
		}
	}
	return pins
}

// JSONDecoder implements a simple JSON decoder which caches unmarshalled data between calls.
type JSONDecoder struct {
	data map[string]interface{}
}

// NewJSONDecoder returns a pointer to a newly initialized JSONDecoder.
// An error is returned if the given string cannot be unmarshalled as JSON.
func NewJSONDecoder(jsn string) (*JSONDecoder, error) {
	var dec JSONDecoder
	if err := json.Unmarshal([]byte(jsn), &dec.data); err != nil {
		return nil, err
	}
	return &dec, nil
}

// Int returns an integer value for a given key, or an error if one is not found.
func (dec *JSONDecoder) Int(key string) (int, error) {
	v := dec.data[key]
	if v == nil {
		return -1, errNoKey
	}
	n, ok := v.(float64)
	if !ok {
		return -1, errors.New(key + " is not an int")
	}
	return int(n), nil
}

// String returns a string value for a given key, or an error if one is not found.
func (dec *JSONDecoder) String(key string) (string, error) {
	if dec.data[key] == nil {
		return "", errors.New(key + " string not found")
	}
	v, ok := dec.data[key].(string)
	if !ok {
		return "", errors.New(key + " not a string")
	}
	return v, nil
}
