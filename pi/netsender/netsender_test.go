package netsender

/*
NAME
  netsender - common package for NetSender clients.

DESCRIPTION
  See Readme.md

AUTHOR
  Dan Kortschak <dan@ausocean.org>

LICENSE
  netsender is Copyright (C) 2017-2018 the Australian Ocean Lab (AusOcean).

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with netsender in gpl.txt.  If not, see [GNU licenses](http://www.gnu.org/licenses).
*/

import (
	"io"
	"io/ioutil"
	"os"
	"reflect"
	"strings"
	"testing"
	"testing/clientest"
	"time"
)

var makePinsTests = []struct {
	csv      string
	restrict string
	want     []Pin
}{
	{
		csv:      "",
		restrict: "",
		want:     nil,
	},
	{
		csv:  "X1,X2,X3",
		want: []Pin{{Name: "X1", Value: -1}, {Name: "X2", Value: -1}, {Name: "X3", Value: -1}},
	},
	{
		csv:  "X1,A2,V3",
		want: []Pin{{Name: "X1", Value: -1}, {Name: "A2", Value: -1}, {Name: "V3", Value: -1}},
	},
	{
		csv:      "X1,A2,V3",
		restrict: "X",
		want:     []Pin{{Name: "X1", Value: -1}},
	},
	{
		csv:      "X1,A2,V3",
		restrict: "X,V",
		want:     []Pin{{Name: "X1", Value: -1}, {Name: "V3", Value: -1}},
	},
	{
		csv:      "X1,A2,V3,",
		restrict: "X,V",
		want:     []Pin{{Name: "X1", Value: -1}, {Name: "V3", Value: -1}},
	},
	{
		csv:      "X1,A2,V3",
		restrict: "X,V,",
		want:     []Pin{{Name: "X1", Value: -1}, {Name: "V3", Value: -1}},
	},
}

func TestMakePins(t *testing.T) {
	for i, test := range makePinsTests {
		got := MakePins(test.csv, test.restrict)
		if !reflect.DeepEqual(got, test.want) {
			t.Errorf("unexpected result for test %d %q/%q:\ngot :%#v\nwant:\n%#v", i, test.csv, test.restrict, got, test.want)
		}
	}
}

var payloadReaderTests = []struct {
	name string
	pins []Pin
	want string
}{
	{
		name: "One character pins",
		pins: []Pin{
			{Data: []byte("H")},
			{Data: []byte("e")},
			{Data: []byte("l")},
			{Data: []byte("l")},
			{Data: []byte("o")},
			{Data: []byte(",")},
			{Data: []byte(" ")},
			{Data: []byte("W")},
			{Data: []byte("o")},
			{Data: []byte("r")},
			{Data: []byte("l")},
			{Data: []byte("d")},
			{Data: []byte("!")},
		},
		want: "Hello, World!",
	},
	{
		name: "Word pins",
		pins: []Pin{
			{Data: []byte("Hello")},
			{Data: []byte(",")},
			{Data: []byte(" ")},
			{Data: []byte("World")},
			{Data: []byte("!")},
		},
		want: "Hello, World!",
	},
}

var readerLimits = []struct {
	name   string
	reader func(io.Reader) io.Reader
}{
	{
		name:   "Normal",
		reader: func(r io.Reader) io.Reader { return r },
	},
	{
		name:   "OneByteReader",
		reader: iotest.OneByteReader,
	},
	{
		name:   "HalfReader",
		reader: iotest.HalfReader,
	},
}

func TestPayloadReaderRead(t *testing.T) {
	for _, test := range payloadReaderTests {
		for _, length := range []int{1, 2, 4, 5, 8, 9, 15, 4 << 10} {
			for _, limit := range readerLimits {
				pr := NewPayloadReader(test.pins)

				r := limit.reader(pr)
				b := make([]byte, length)
				var buf strings.Builder
				for {
					n, err := r.Read(b)
					buf.Write(b[:n])
					if err != nil {
						if err != io.EOF {
							t.Errorf("unexpected error for %s/%s: %v", test.name, limit.name, err)
						}
						break
					}
				}
				got := buf.String()
				if got != test.want {
					t.Errorf("unexpected result for %s/%s:\ngot :%s\nwant:%s", test.name, limit.name, got, test.want)
				}
			}
		}
	}
}

func TestPayloadReaderCopy(t *testing.T) {
	for _, test := range payloadReaderTests {
		for _, limit := range readerLimits {
			pr := NewPayloadReader(test.pins)
			r := limit.reader(pr)
			var buf strings.Builder
			n, err := io.Copy(&buf, r)
			if n != int64(len(test.want)) {
				t.Errorf("unexpected result for %s/%s: got:%d want:%d", test.name, limit.name, n, len(test.want))
			}
			if err != nil {
				t.Errorf("unexpected error for %s/%s: %v", test.name, limit.name, err)
			}
			got := buf.String()
			if got != test.want {
				t.Errorf("unexpected result for %s/%s:\ngot :%s\nwant:%s", test.name, limit.name, got, test.want)
			}
		}
	}
}

var jsonStringTests = []struct {
	jsn  string
	key  string
	want string
	fail bool
}{
	{
		jsn:  `{"er":"InvalidValue"}`,
		key:  "er",
		want: "InvalidValue",
	},
	{
		jsn:  `{"ts":123456789}`,
		key:  "ts",
		fail: true, // wrong type
	},
	{
		jsn:  `{"er":"InvalidValue"}`,
		key:  "ma",
		fail: true, // wrong key
	},
}

var jsonIntTests = []struct {
	jsn  string
	key  string
	want int
	fail bool
}{
	{
		jsn:  `{"ts":123456789}`,
		key:  "ts",
		want: 123456789,
	},
	{
		jsn:  `{"ts":"123456789"}`,
		key:  "ts",
		fail: true, // wrong type
	},
	{
		jsn:  `{"ts":123456789}`,
		key:  "dk",
		fail: true, // wrong key
	},
}

func TestJSONDecoder(t *testing.T) {
	for i, test := range jsonStringTests {
		dec, err := NewJSONDecoder(test.jsn)
		if err != nil {
			t.Errorf("unexpected error for string test %d: %v", i, err)
		}
		got, err := dec.String(test.key)
		if err == nil && got == test.want || err != nil && test.fail {
			continue
		}
		t.Errorf("unexpected result for string test %d, key %s: got %s, want %s\n", i, test.key, got, test.want)
	}
	for i, test := range jsonIntTests {
		dec, err := NewJSONDecoder(test.jsn)
		if err != nil {
			t.Errorf("unexpected error for int test %d: %v", i, err)
		}
		got, err := dec.Int(test.key)
		if err == nil && got == test.want || err != nil && test.fail {
			continue
		}
		t.Errorf("unexpected result for int test %d, key %s: got %d, want %d\n", i, test.key, got, test.want)
	}
}

const (
	testConfig          = "ma 00:00:00:00:00:01\ndk 10000001\nsh data.cloudblue.org\n" // contents of the netsender.conf used for testing.
)

// TestSync tests synchronization of client mode and error values with NetReceiver.
func TestSync(t *testing.T) {
	file, err := createNetsenderConfig()
	if err != nil {
		t.Errorf("createNetsenderConfig failed with error %v", err)
	}
	defer os.Remove(file)

	// Create a NetSender client.
	var logger testLogger
	ns, err := New(&logger, nil, nil, nil, WithConfigFile(file))
	if err != nil {
		t.Errorf("netsender.New failed with error %v", err)
	}

	// Check our mode is Normal to begin with.
	_, err = ns.Vars()
	if err != nil {
		t.Errorf("ns.Vars failed with error %v", err)
	}
	if ns.Mode() != "Normal" {
		t.Errorf("Expected Normal for ns.Mode(), got %s", ns.Mode())
	}

	// Set our mode & error to Paused and TestError respectively.
	ns.setModeAndEror(t, "Paused", "TestError")

	// Now reset our mode & error for next time.
	ns.setModeAndEror(t, "Normal", "")
}

// TestTimeout tests timeout.
func TestTimeout(t *testing.T) {
	file, err := createNetsenderConfig()
	if err != nil {
		t.Errorf("createNetsenderConfig failed with error %v", err)
	}
	defer os.Remove(file)

	// Create a NetSender client.
	var logger testLogger
	ns, err := New(&logger, nil, nil, nil, WithConfigFile(file))
	if err != nil {
		t.Errorf("netsender.New failed with error %v", err)
	}

	// First call Vars with the default timeout.
	_, err = ns.Vars()
	if err != nil {
		t.Errorf("ns.Vars failed failed with error %v", err)
	}

	// Now call Vars with an insanely small timeout.
	Timeout = 1 * time.Millisecond
	_, err = ns.Vars()
	if err == nil {
		t.Errorf("ns.Vars failed to time out")
	}
}

// createNetsenderConfig creates a temporary netsender.conf file and returns the name.
func createNetsenderConfig() (name string, err error) {
	// Create a config file.
	f, err := ioutil.TempFile("", "netsender.conf")
	if err != nil {
		return
	}
	name = f.Name()
	_, err = f.Write([]byte(testConfig))
	if err == nil {
		err = f.Close()
	}
	if err != nil {
		os.Remove(name)
		return
	}

	return name, nil
}

// setModeAndEror sets the mode and error and then tests that the values are as expected.
func (ns *Sender) setModeAndEror(t *testing.T, mode, error string) {
	vs := ns.VarSum()
	ns.SetMode(mode, &vs)
	ns.SetError(error, &vs)
	if vs != 0 {
		t.Errorf("Expected 0 for vs, got %d", vs)
	}
	vars, err := ns.Vars()
	if err != nil {
		t.Errorf("ns.Vars failed with error %v", err)
	}
	if ns.Mode() != mode {
		t.Errorf("Expected \"%s\" for ns.Mode(), got \"%s\"", mode, ns.Mode())
	}
	if vars["mode"] != mode {
		t.Errorf("Expected \"%s\" for vars[\"mode\"], got \"%s\"", mode, vars["mode"])
	}
	if ns.Error() != error {
		t.Errorf("Expected \"%s\" for ns.Error(), got \"%s\"", error, ns.Error())
	}
	if vars["error"] != error {
		t.Errorf("Expected \"%s\" for vars[\"error\"], got \"%s\"", error, vars["error"])
	}
}

// testLogger implements a netsender.Logger.
type testLogger struct{}

// SetLevel normally sets the logging level, but in our case it is a no-op.
func (tl *testLogger) SetLevel(level int8) {
}

// Log normally logs a message, but in our case it just checks that the log level is valid.
func (tl *testLogger) Log(level int8, msg string, params ...interface{}) {
	if level < -1 || level > 5 {
		panic("Invalid log level")
	}
}

// TestNetSpeedTests tests the TestDownload and TestUpload methods.
func TestNetSpeedTests(t *testing.T) {
	ns := &Sender{
		download: -1,
		upload:   -1,
		services: map[string]string{
			"default": "data.cloudblue.org",
		},
		logger: &testLogger{},
	}

	// Test download.
	err := ns.TestDownload()
	if err != nil {
		t.Errorf("unexpected error from TestDownload(): %v", err)
	}
	t.Logf("download: %d bps", ns.download)

	// Test upload.
	err = ns.TestUpload()
	if err != nil {
		t.Errorf("unexpected error from TestUpload(): %v", err)
	}
	t.Logf("upload: %d bps", ns.upload)
}
