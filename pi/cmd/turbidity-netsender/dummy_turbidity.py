# DESCRIPTION
#   dummy_turbidity.py produces dummy interger values in the same format produced
#   by the Remond turbidity sensor, facilitating functionality testing of
#   turbidity.go via turbidity_test.go.
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

import random
import time
import sys

# Constants.
FMT_LINE = "??-"
DELAY    = 1 #second.

# Returns dummy turbidity value in same format as remond Turbidity Sensor.
# Int range is arbitrary but set to 3400 < n < 3500 to allow for expecations.
def main():
    while True:
        turbidity    = random.randint(3400,3500)
        print(FMT_LINE)
        sys.stdout.flush()
        print(turbidity)
        sys.stdout.flush()
        time.sleep(DELAY)

if __name__ == "__main__":
    main()
