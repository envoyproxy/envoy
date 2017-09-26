#!/bin/bash

set -e

mkdir -p "${THIRDPARTY_DEPS}"
mkdir -p "${THIRDPARTY_BUILD}"
mkdir -p "${THIRDPARTY_SRC}"

if [ -z "$NUM_CPUS" ]; then
  case `uname` in
      Darwin)
          NUM_CPUS=`sysctl hw.physicalcpu | cut -f 2 -d' '`;;
      *)
          NUM_CPUS=`grep -c ^processor /proc/cpuinfo`;;
  esac
fi

# Invokers can set BUILD_CONCURRENCY=0 to ensure each build recipe is invoked sequentially, with all
# CPU resources available. This is useful when debugging build performance.
if [[ "${BUILD_CONCURRENCY}" == "0" ]]
then
  for dep in "$@"
  do
    make -C "$(dirname "$0")" -j "${NUM_CPUS}" "$dep"
  done
else
  make -C "$(dirname "$0")" -j "${NUM_CPUS}" "$@"
fi
