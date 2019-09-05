#!/bin/bash

# Simple script to clone vM to vN API, performing sed-style heuristic fixup of
# build paths and package references.
#
# Usage:
#
# ./tools/api/clone.sh v2 v3

set -e

declare -r OLD_VERSION="$1"
declare -r NEW_VERSION="$2"

# For vM -> vN, replace //$1*/vMalpha\d* with //$1*/vN in BUILD file $2
# For vM -> vN, replace //$1*/vM with //$1*/vN in BUILD file $2
function replace_build() {
  sed -i -e "s#\(//$1[^\S]*\)/${OLD_VERSION}alpha[[:digit:]]*#\1/${NEW_VERSION}#g" "$2"
  sed -i -e "s#\(//$1[^\S]*\)/${OLD_VERSION}#\1/${NEW_VERSION}#g" "$2"
}

# For vM -> vN, replace $1*[./]vMalpha with $1*[./]vN in .proto file $2
# For vM -> vN, replace $1*[./]vM with $1*[./]vN in .proto file $2
function replace_proto() {
  sed -i -e "s#\($1\S*[\./]\)${OLD_VERSION}alpha[[:digit:]]*#\1${NEW_VERSION}#g" "$2"
  sed -i -e "s#\($1\S*[\./]\)${OLD_VERSION}#\1${NEW_VERSION}#g" "$2"
}

# We consider both {vM, vMalpha} to deal with the multiple possible combinations
# of {vM, vMalpha} existence for a given package.
for p in $(find api/ -name "${OLD_VERSION}*")
do
  declare PACKAGE_ROOT="$(dirname "$p")"
  declare OLD_VERSION_ROOT="${PACKAGE_ROOT}/${OLD_VERSION}"
  declare NEW_VERSION_ROOT="${PACKAGE_ROOT}/${NEW_VERSION}"

  # Deal with the situation where there is both vM and vMalpha, we only want vM.
  if [[ -a "${OLD_VERSION_ROOT}" && "$p" != "${OLD_VERSION_ROOT}" ]]
  then
    continue
  fi

  # Copy BUILD and .protos across
  rsync -a "${p}"/ "${NEW_VERSION_ROOT}/"

  # Update BUILD files with vM -> vN
  for b in $(find "${NEW_VERSION_ROOT}" -name BUILD)
  do
    replace_build envoy "$b"
    # Misc. cleanup for go BUILD rules
    sed -i -e "s#\"${OLD_VERSION}\"#\"${NEW_VERSION}\"#g" "$b"
  done

  # Update .proto files with vM -> vN
  for f in $(find "${NEW_VERSION_ROOT}" -name "*.proto")
  do
    replace_proto envoy "$f"
    replace_proto api "$f"
    replace_proto service "$f"
    replace_proto common "$f"
    replace_proto config "$f"
    replace_proto filter "$f"
    replace_proto "" "$f"
  done
done
