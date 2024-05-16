# DESCRIPTION
#   raspiSerial.py gives functionality to the Turbidity Sensor by reading
#   values of the Arduino Uno through the Raspberry Pi serial USB port. 
#   This script is called by NewTurbiditySensor in turbidity.go.
#
# AUTHORS
#   Harrison Telford <harrison@ausocean.org>
#
# LICENSE
#   Copyright (C) 2020-21 the Australian Ocean Lab (AusOcean)
#
#   It is free software: you can redistribute it and/or modify them
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   It is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
#   for more details.
#
#   You should have received a copy of the GNU General Public License
#   in gpl.txt.  If not, see http://www.gnu.org/licenses.

import serial
import time
import sys

#  Const.
DELAY     = 0.1 #seconds
BAUD_RATE = 9600

ser=serial.Serial('/dev/ttyACM0',BAUD_RATE)

def main():
    while True:
        turbidity = ser.readline()
        print(turbidity.decode('latin1'))
        sys.stdout.flush()
        time.sleep(DELAY)

if __name__ == "__main__":
    main()