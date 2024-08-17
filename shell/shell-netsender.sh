#!/bin/bash -e
# shell-netsender is a simple NetSender client implemented as a shell script.
#
# Client capabalities:
# - Configuring MAC address (ma), device key (dk), client type (ct),
#   and monitor period (mp) and update /etc/netsender.conf
# - Performing an upgrade.
# - Rebooting.
Version="v0.2.0"
ConfigFile="/etc/netsender.conf"
LogFile="/var/log/netsender/netsender.log"
URL="http://data.cloudblue.org"

if [ "$1" == "-Version" ] || [ "$1" == "-v" ]; then
  echo "$Version"
  exit 0
fi

if [ ! -f "$ConfigFile" ]; then
  log "Error" "$ConfigFile not found"
  exit 1
fi

# log outputs <time>:<log-level>:<message> to $LogFile
function log() {
  now=$(date '+%Y-%m-%d %H:%M:%S')
  echo "$now: $1: $2" >> $LogFile
}

# getParam outputs a config param, if any.
function getParam() {
  param=$1
  cfg=$2
  val=  
  while IFS= read -r line; do
    if [[ ${line:0:2} == "$param" ]]; then
      val=${line:3}
      break
    fi
  done <<< "$cfg"
  echo $val
}

cfg=$(cat "$ConfigFile")
MA=$(getParam ma "$cfg")
DK=$(getParam dk "$cfg")
CT=$(getParam ct "$cfg")
MP=$(getParam mp "$cfg")

if [ -z "$MP" ]; then
  MP=60
fi
if [ -z "$CT" ]; then
  CT=test
fi

# configure updates ma, dk, ct and mp params.
# NB: Updating MAC addresses does NOT reconfigure eth0.
function configure() {
  url="$URL/config?ma=$MA&dk=$DK"
  if [ -n "$@" ]; then
    # Append optional query params.
    url="$url&$@"
  fi
  log "Info" "GET $url"
  resp=$(curl -s "$url")
  v=$(jq -r ".ma" <<< "$resp")
  if [ -n "$v" ] && [ "$v" != "null" ]; then
    MA="$v"
    log "Info" "ma=$MA"
  fi
  v=$(jq -r ".dk" <<< "$resp")
  if [ -n "$v" ] && [ "$v" != "null" ]; then
    DK="$v"
    log "Info" "dk=$DK"
  fi
  v=$(jq -r ".ct" <<< "$resp")
  if [ -n "$v" ] && [ "$v" != "null" ]; then
    CT="$v"
    log "Info" "ct=$CT"
  fi
  v=$(jq -r ".mp" <<< "$resp")
  if [ -n "v" ] && [ "$v" != "null" ]; then
    MP="$v"
    log "Info" "mp=$MP"
  fi
  sudo echo -e "ma:$MA\ndk:$DK\nct:$CT\nmp:$MP" > $ConfigFile
  sudo chown "$USER" $ConfigFile
  log "Info" "$ConfigFile updated."
}

log "Info" "Restarting"
while true; do
  url="$URL/poll?ma=$MA&dk=$DK"
  log "Info" "GET $url"
  resp=$(curl -s "$url")
  er=$(jq -r ".er" <<< "$resp")
  if [ -n "$er" ] && [ "$er" != "null" ]; then
    log "Error" "$er"
  fi

  rc=$(jq -r ".rc" <<< "$resp")
  if [ -z "$rc" ] || [ "$rc" == "null" ]; then
    sleep "$MP"
    continue
  fi

  case "$rc" in      
    "0")
      ;;
    "1")
      log "Info" "Received update request"
      configure
      ;;
    "2")
      log "Info" "Received reboot request"
      sudo reboot
      ;;
  
    "4")
      log "Info" "Received upgrade request"
      log "Info" "pkg-upgrade.sh $CT @latest prereq"
      bash -c "pkg-upgrade.sh $CT @latest prereq"
      if [ $? -eq 0 ]; then
	log "Info" "Upgrade succeeded"
        configure "md=Completed"
      else
	log "Error" "Upgrade failed"
      fi
      ;;
  
    *)
      log "Info" "Ignoring $rc request"
      ;;
  esac
  sleep "$MP"
done

