#!/bin/bash

set -e

function check {
  NEEDS_ICU=$(readelf -d "${1}" | grep 'NEEDED' | grep 'icu')
  if [[ "${NEEDS_ICU}" != "" ]]; then
     echo "${1} has dependecy to ICU ${NEEDS_ICU}"
     exit 1
  fi
}

# Validate the test file.
TEST_FILE=$(realpath bazel-bin/third_party/icu/googleurl/googleurl_test.runfiles/envoy/third_party/icu/googleurl/googleurl_test)
check "${TEST_FILE}"
