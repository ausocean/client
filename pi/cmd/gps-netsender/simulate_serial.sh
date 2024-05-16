#!/bin/bash
# Simulate the output of a GPS sensor
sleep 3

while true; do
	while IFS='' read -r line || [[ -n "$line" ]]; do
		echo -ne "$line\r\n"
		sleep 1
	done < "1.log"
done
