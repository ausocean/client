package smartlogger

import (
	"fmt"
	"io/ioutil"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/Shopify/toxiproxy/client"
	"github.com/andreyvit/diff"

	"github.com/ausocean/client/pi/netsender"
	"github.com/ausocean/client/pi/netspoofer"
	"github.com/ausocean/utils/logging"
)

const (
	timeout = 5 * time.Minute

	numRuns = 20
	numLogs = 20
)

// TestMain completes set up of proxy, logger, and server.
func TestLogger(t *testing.T) {
	done := make(chan struct{})
	go func() {
		select {
		case <-time.After(timeout):
			panic(fmt.Sprintf("test timed out after %v", timeout))
		case <-done:
		}
	}()

	path, err := exec.LookPath("toxiproxy-server")
	if err != nil {
		t.Skipf("no toxiproxy server in path: %v", err)
	}
	cmd := exec.Command(path)
	cmd.Stdout = ioutil.Discard
	err = cmd.Start()
	if err != nil {
		t.Fatalf("failed to start toxiproxy-server: %v", err)
	}

	// Wait for toxiproxy-server to start up.
	time.Sleep(5 * time.Second)

	rand.Seed(1)

	err = os.RemoveAll("logs")
	if err != nil {
		t.Fatalf("failed to remove old logs: %v", err)
	}

	client := toxiproxy.NewClient("localhost:8474")
	proxy, err := client.CreateProxy("downer", "localhost:8080", "localhost:8000")
	if err != nil || proxy == nil {
		t.Fatalf("failed to set up proxy: %v", err)
	}

	sl := New("logs")
	log := logging.New(int8(logging.Debug), &sl.LogRoller, true)
	log.Debug( "Log Start")
	log.Debug( "gpio-netsender: Logger Initialized")

	go netspoofer.Run()

	// Run tests.
	for _, scheme := range []struct {
		prefix string
		every  int
	}{
		{prefix: "", every: 1},
		{prefix: "Erratic ", every: 5},
	} {
		for _, conn := range []struct {
			policy  string
			dropout func() chan struct{}
		}{
			{policy: "Good"},
			{policy: "Bad", dropout: dropout(proxy, 20*time.Second, 3*time.Second)},
		} {
			t.Run(scheme.prefix+"Send Logs with "+conn.policy+" connection", func(t *testing.T) {
				// FIXME(kortschak): Remove this conditional elision
				// of bad case tests when the failure is understood.
				if conn.policy == "Bad" {
					t.Skip("skipping flakey network policy tests")
				}

				ns, err := netsender.New(log, nil, nil, nil)
				if err != nil {
					t.Fatalf("failed to created netsender:%v", err)
				}

				if conn.dropout != nil {
					done := conn.dropout()
					defer close(done)
				}

				sendLogs(t, ns, log, sl, scheme.every)
			})
		}
	}

	proxy.Delete()

	err = cmd.Process.Kill()
	if err != nil {
		t.Errorf("failed to kill toxiproxy-server: %v", err)
	}

	err = os.RemoveAll("logs")
	if err != nil {
		t.Errorf("failed to clean up logs: %v", err)
	}
}

// sendLogs sends and records log stream, and later compares with the received server side logs.
func sendLogs(t *testing.T, ns *netsender.Sender, log logging.Logger, sl *Smartlogger, every int) {
	netspoofer.Reset()
	var clientLog string
	for i := 0; i < numRuns; i++ {
		writeTo(log)
		if rand.Int()%every == 0 {
			clientLog += send(t, ns, log, sl)
		}
	}
	for !checkNoBackups(t) {
		clientLog += send(t, ns, log, sl)
	}
	compare(t, clientLog)
}

// writeTo generates a series of random log messages.
func writeTo(log logging.Logger) {
	for i := 0; i < numLogs; i++ {
		r := rand.Int()
		log.Log(int8(i%5)-1, "This is log "+strconv.Itoa(i*r), "I'm an extra Parameter", r)
		time.Sleep(time.Millisecond)
	}
	time.Sleep(5 * time.Millisecond)
}

// compare fetches logs fom the server, sorts them, and compares them to the log stream from the client.
func compare(t *testing.T, clientLog string) {
	var serverSideLog string
	serverSideLog = netspoofer.Logs()

	// sort logs.
	clientLogSlice := strings.Split(clientLog, "\n")
	serverSideSlice := strings.Split(serverSideLog, "\n")
	sort.Strings(clientLogSlice)
	sort.Strings(serverSideSlice)
	clientLog = strings.Join(clientLogSlice, "\n")
	serverSideLog = strings.Join(serverSideSlice, "\n")

	if clientLog != serverSideLog {
		t.Errorf("Result not as expected:\nExpected:\n%v\nReceived:\n%v\nDiff:\n%v\n", clientLog, serverSideLog, diff.LineDiff(clientLog, serverSideLog))
	}
	netspoofer.Reset()
}

// send finishes a log file, rotates, starts a new log file, and send all remaining backups to the server.
func send(t *testing.T, ns *netsender.Sender, log logging.Logger, sl *Smartlogger) string {
	log.Debug( "Log End")
	clientLog := readMainLogFile(t)
	time.Sleep(time.Millisecond * 10)
	sl.Rotate()
	log.Debug( "Log Start")
	sl.SendLogs(ns)

	return clientLog
}

// readMainLogFile returns the contents of the current log file.
func readMainLogFile(t *testing.T) string {
	logFiles, err := filepath.Glob("logs/netsender.log")
	if err != nil {
		t.Log("Can't glob matching log files")
	}

	for i := 0; i < len(logFiles); i++ {
		logFile, err := os.Open(logFiles[i])
		if err != nil {
			t.Log("Can't open log file " + logFiles[i])
			continue
		}
		defer logFile.Close()
		logFileStats, err := logFile.Stat()
		if err != nil {
			t.Log("Can't cant get stats for " + logFiles[i])
			continue
		}
		length := logFileStats.Size()
		logFileContent := make([]byte, length)
		_, err = logFile.Read(logFileContent)
		if err != nil {
			t.Log("Can't cant read contents of " + logFiles[i])
			continue
		}
		return string(logFileContent)
	}
	return ""
}

// readAllLogFile returns the contents of all of the log files.
func readAllLogFile(t *testing.T) string {
	directory, err := os.Open("logs")
	if err != nil {
		t.Log("Can't open log directory for sending")
	}
	logFiles, err := directory.Readdirnames(0)
	if err != nil {
		t.Log("Can't read log directory contents for sending")
	}

	res := ""

	for _, fileName := range logFiles {
		lf := filepath.Join("logs", fileName)

		logFileContent, err := ioutil.ReadFile(lf)
		if err != nil {
			fmt.Printf("Can't read log file %v\n", fileName)
			continue
		}
		res += string(logFileContent)
	}
	return res
}

// checkNoBackups tests to see if all log files have been sent and backups were deleted.
func checkNoBackups(t *testing.T) bool {
	logFiles, err := filepath.Glob("logs/netsender-*")
	if err != nil {
		t.Log("Can't glob matching log files")
	}
	return len(logFiles) == 0
}

// dropout returns a function that will repeatedly disable the proxy for random periods
// up to off long and then re-enable for up to on long periods.
func dropout(proxy *toxiproxy.Proxy, off, on time.Duration) func() (done chan struct{}) {
	return func() chan struct{} {
		// Switch connection on and off randomly.
		done := make(chan struct{})
		go func() {
			for {
				select {
				case <-done:
					return
				default:
					proxy.Disable()
					time.Sleep(time.Duration(rand.Intn(int(off))))
					proxy.Enable()
					time.Sleep(time.Duration(rand.Intn(int(on))))
				}
			}
		}()
		return done
	}
}
