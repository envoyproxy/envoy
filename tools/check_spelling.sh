#!/bin/bash

# Applies requisite code formatters to the source tree
# check_spelling.sh

set -e

VERSION="0.3.4"
OS=""

MISSPELL_ARGS="-error -o stderr"

if [[ $1 == "fix" ]];then
  MISSPELL_ARGS="-w"
fi

if [[ `uname` == "Darwin" ]];then
  OS="mac"
elif [[ `uname` == "Linux" ]];then
  OS="linux"
else
  echo "Current only support mac/Linux"
  exit 1
fi

SCRIPTPATH=$( cd "$(dirname "$0")" ; pwd -P )
ROOTDIR=$SCRIPTPATH/..
cd "$ROOTDIR"

# Install tools we need
if [[ ! -e "/tmp/misspell"  ]];then
  wget -qO- https://github.com/client9/misspell/releases/download/v${VERSION}/misspell_${VERSION}_${OS}_64bit.tar.gz|tar xvz -C /tmp &> /dev/null
fi

chmod +x /tmp/misspell
 
# Spell checking
# All the skipping files are defined in tools/spelling_failures
skipping_file="${ROOTDIR}/tools/spelling_failures"

# All the ignore words ar defained in tools/ignore_words
ignore_words_file="${ROOTDIR}/tools/ignore_words"

ignore_words=$(echo -n `cat ${ignore_words_file} | grep -v "^#"|grep -v "^$"` | tr ' ' ',')
failing_packages=$(echo `cat ${skipping_file}` | sed "s| | -e |g")
git ls-files | grep -v -e ${failing_packages} | xargs /tmp/misspell -i "${ignore_words}" ${MISSPELL_ARGS}

