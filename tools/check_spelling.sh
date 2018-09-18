#!/bin/bash

# Applies requisite code formatters to the source tree
# check_spelling.sh

set -u
set -e

VERSION="0.3.4"
OS=""

MISSPELL_ARGS="-error -o stderr"

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 check|fix"
  exit -1
fi

if [[ $1 == "fix" ]]; then
  MISSPELL_ARGS="-w"
fi

if [[ `uname` == "Darwin" ]]; then
  OS="mac"
elif [[ `uname` == "Linux" ]]; then
  OS="linux"
else
  echo "Current only support mac/Linux"
  exit 1
fi

SCRIPTPATH=$( cd "$(dirname "$0")" ; pwd -P )
ROOTDIR=$SCRIPTPATH/..
cd "$ROOTDIR"

BIN_FILENAME="misspell_${VERSION}_${OS}_64bit.tar.gz"
# Install tools we need
if [[ ! -e "/tmp/misspell" ]]; then
  if ! wget https://github.com/client9/misspell/releases/download/v${VERSION}/${BIN_FILENAME} -O /tmp/${BIN_FILENAME} --no-verbose -t 3  -o /tmp/wget.log; then
    cat /tmp/wget.log
    exit -1
  fi
  tar -xvf /tmp/${BIN_FILENAME} -C /tmp &> /dev/null
fi

chmod +x /tmp/misspell
 
# Spell checking
# All the skipping files are defined in tools/spelling_skip_files.txt
spelling_skip_files="${ROOTDIR}/tools/spelling_skip_files.txt"

# All the ignore words ar defained in tools/spelling_whitelist_words.txt
spelling_whitelist_words_file="${ROOTDIR}/tools/spelling_whitelist_words.txt"

whitelist_words=$(echo -n `cat ${spelling_whitelist_words_file} | grep -v "^#"|grep -v "^$"` | tr ' ' ',')
skip_files=$(echo `cat ${spelling_skip_files}` | sed "s| | -e |g")
git ls-files | grep -v -e ${skip_files} | xargs /tmp/misspell -i "${whitelist_words}" ${MISSPELL_ARGS}
