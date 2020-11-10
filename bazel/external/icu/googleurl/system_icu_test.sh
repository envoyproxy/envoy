#!/bin/bash

set -e

TEST_BIN="${TEST_SRCDIR}/envoy/bazel/external/icu/googleurl/googleurl_test"

if [[ $(uname) == "Darwin" ]]; then
  echo "Skipping on macOS."
  exit 0
fi

if readelf -d "${TEST_BIN}" | grep "NEEDED" | grep "icu"; then
  echo "${TEST_BIN} is linked to system ICU."
  exit 1
fi

echo "${TEST_BIN} is not linked to system ICU."
exit 0
