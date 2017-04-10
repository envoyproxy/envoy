#!/bin/bash

set -e

mkdir -p "${THIRDPARTY_DEPS}"
mkdir -p "${THIRDPARTY_BUILD}"
mkdir -p "${THIRDPARTY_SRC}"

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`
make -C "$(dirname "$0")" -j "${NUM_CPUS}"
