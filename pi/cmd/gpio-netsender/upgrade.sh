#!/bin/bash
# All-purpose upgrade script.
# Upgrades source(s) to given Git tag, runs make in each directory,
# and write tags to tags.conf upon success, exiting 0.
# NB: Customize SrcDirs as needed to reflect dependencies.
Usage="Usage: upgrade.sh [-d] tag"
BaseDir=$GOPATH/src/bitbucket.org/ausocean
VarDir=/var/netsender
LogFile=/var/log/netsender/stream.log
SrcDirs=($BaseDir/utils $BaseDir/iot)
if [ "$1" == "-d" ]; then
    set  -x
    GitFlags=""
    NewTag="$2"
else
    # capture stdout and stderr
    exec 2> $LogFile
    exec 1>&2
    GitFlags="--quiet"
    NewTag="$1"
fi
if [ -z "$GOPATH" ]; then
    echo "Error: GOPATH not defined"
    exit 1
fi
if [ -z "$NewTag" ]; then
    echo "$Usage"
    exit 1
fi
for dir in ${SrcDirs[@]}; do
  pushd $dir
  if [ ! "$?" == 0 ]; then
    exit 1
  fi
  git fetch $GitFlags --depth=1 origin refs/tags/$NewTag:refs/tags/$NewTag
  if [ ! "$?" == 0 ]; then
    exit 1
  fi
  git checkout $GitFlags --force tags/$NewTag
  if [ ! "$?" == 0 ]; then
    exit 1
  fi
  if [ -e Makefile ]; then
    make
    if [ ! "$?" == 0 ]; then
      exit 1
    fi
  fi
  popd
done
if [ ! -d "$VarDir" ]; then
  echo "Error: $VarDir does not exit."	
  exit 1
fi
git tag > "$VarDir/tags.conf"
exit $?
