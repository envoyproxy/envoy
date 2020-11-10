#!/bin/bash

set -e

function check {
  NEEDS_ICU=$(readelf -d "${1}" | grep 'NEEDED' | grep 'icu')
  if [[ "${NEEDS_ICU}" != "" ]]; then
     echo "${1} has dependecy to ICU ${NEEDS_ICU}"
     exit 1
  fi
}

# Check if "googleurl_test" has dependency to system ICU.
GOOGLE_URL_TEST_RUN_FILES="bazel-bin/third_party/icu/googleurl/googleurl_test.runfiles"
check "${GOOGLE_URL_TEST_RUN_FILES}"/envoy/third_party/icu/googleurl/googleurl_test
