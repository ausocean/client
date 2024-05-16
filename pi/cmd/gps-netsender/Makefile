.PHONY: all simulate-serial

all:
	go build

simulate-serial:
	socat PTY,link=./virtual-tty,raw,echo=0 EXEC:./simulate_serial.sh
