#!/bin/bash

set -e

ENVOY_BIN="${TEST_SRCDIR}/envoy/source/exe/envoy-static"

if [[ `uname` == "Darwin" ]]; then
  echo "Skipping on macOS."
  exit 0
fi

if readelf -hW "${ENVOY_BIN}" | grep "Type" | grep -o "DYN (Shared object file)"; then
  echo "${ENVOY_BIN} is a PIE!"
  exit 0
fi

echo "${ENVOY_BIN} is not a PIE!"
exit 1
