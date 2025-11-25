#!/usr/bin/env bash

set -e

ENVOY_BIN="${TEST_SRCDIR}/envoy/source/exe/envoy-static"


if [[ $(uname) == "Darwin" ]]; then
  echo "Skipping on macOS."
  exit 0
fi

# Try to find llvm-readelf, failing that fallback to non-hermetic readelf in PATH
READELF="$(find "${RUNFILES_DIR}" -name llvm-readelf -type f | head -n1 || :)"
if [[ -z "$READELF" ]]; then
    READELF="$(command -v llvm-readelf || :)"
fi
if [[ -z "$READELF" ]]; then
    READELF="$(command -v readelf || :)"
fi

if [[ -z "$READELF" ]]; then
    echo "Unable to find readelf binary" >&2
    exit 1
fi

if "$READELF" -hW "${ENVOY_BIN}" | grep "Type" | grep -o "DYN (Shared object file)"; then
  echo "${ENVOY_BIN} is a PIE!"
  exit 0
fi
if "$READELF" -hW "${ENVOY_BIN}" | grep "Type" | grep -o "DYN (Position-Independent Executable file)"; then
  echo "${ENVOY_BIN} is a PIE!"
  exit 0
fi

echo "${ENVOY_BIN} is not a PIE!"
exit 1
