#run before running code:
sudo pigpiod

#to install pigpio:
sudo apt-get install python-pigpio python3-pigpio

#to install busio:
Run the standard updates:

sudo apt-get update

sudo apt-get upgrade

and

sudo pip3 install --upgrade setuptools

If above doesn't work try

sudo apt-get install python3-pip

cd ~
sudo pip3 install --upgrade adafruit-python-shell
wget https://raw.githubusercontent.com/adafruit/Raspberry-Pi-Installer-Scripts/master/raspi-blinka.py
sudo python3 raspi-blinka.py

#install LSM303 libraries:
sudo pip3 install adafruit-circuitpython-lsm303-accel
sudo pip3 install adafruit-circuitpython-lsm303dlh-mag


bearing.py:
  attemps to align servo based on heading from LSM303 accel/mag module.
magnetometer.py:
  outputs LSM303 magnetometer readings (i.e. xyz field strength).
heading.py:
  outputs calculated heading from LSM303 accel and mag.

If any of this code is run on a system where the adafruit LSM303 modules are
not found, the "dummy" code is used to provide objects with pseudo sensor
values reported. 
