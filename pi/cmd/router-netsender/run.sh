#!/bin/sh -e
# This script launches router-netsender on a pi, intended to run at boot time.

echo Set kernel parameters:
# kernel settings to improve performance on Raspberry Pi
# tell Linux to fork optimistically
sudo sysctl -w vm.overcommit_memory=1
# minimize swapping, without disabling it completely
sudo sysctl -w vm.swappiness=1

# the following required directories _should_ already exist
if [ ! -d /var/log/netsender ]; then
  sudo mkdir /var/log/netsender
  chmod guo+rwx /var/log/netsender
fi
if [ ! -d /var/netsender ]; then
  sudo mkdir /var/netsender
  chmod guo+rwx /var/netsender
fi

# show IP addresses
echo Our IP addresses:
sudo ip addr show | grep inet

# capture stdout and stderr to a secondary log file (just in case)
exec 2> /var/log/netsender/std.log
exec 1>&2

# read configured password for router from config file.
PASS=$(cat /etc/router-netsender.conf | grep pw | awk '{print $2}')

# set env, working dir and run router-netsender
HOME=/home/pi
GOPATH=$HOME/go
PROGPATH=$GOPATH/src/bitbucket.org/ausocean/iot/pi/cmd/router-netsender
PATH=$PATH:/usr/local/go/bin:$PROGPATH
cd $PROGPATH
sudo HOME=$HOME GOPATH=$GOPATH PATH=$PATH ./router-netsender -password $PASS
if [ $? -eq 0 ]
then
  echo "Successfully exited router-netsender"
  exit 0
else
  echo "router-netsender exited with code: $?" >&2
  exit 1
fi
