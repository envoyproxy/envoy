#!/bin/bash

# Applies requisite code formatters to the source tree
# check_spelling.sh

set -e

IGNORE_STRING="Creater,creater,ect,overriden"
MISSPELL_ARGS="-error -o stderr"

if [[ $1 == "fix" ]];then
  MISSPELL_ARGS="-w"
fi

SCRIPTPATH=$( cd "$(dirname "$0")" ; pwd -P )
ROOTDIR=$SCRIPTPATH/..
cd "$ROOTDIR"

GOPATH=$(cd "/tmp/"; pwd)
export GOPATH
export PATH=$GOPATH/bin:$PATH

# Install tools we need, but only from vendor/...
go get -u github.com/client9/misspell/cmd/misspell

# Spell checking
# All the skipping files are defined in bin/.spelling_failures
skipping_file="${ROOTDIR}/tools/.spelling_failures"
failing_packages=$(echo `cat ${skipping_file}` | sed "s| | -e |g")
git ls-files | grep -v -e ${failing_packages} | xargs misspell -i "${IGNORE_STRING}" ${MISSPELL_ARGS}

