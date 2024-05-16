all:	netsender gpio-netsender

netsender:
	cd pi/netsender; go build

gpio-netsender:
	cd pi/cmd/gpio-netsender; go build