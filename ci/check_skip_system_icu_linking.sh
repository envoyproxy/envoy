#!/bin/bash

set -e

ls -la

ls -la bazel-bin/third_party

BINARY_NEEDS_ICU=$(readelf -d bazel-bin/third_party/icu/googleurl/googleurl_test | grep 'NEEDED' | grep 'icu')
if [[ "${BINARY_NEEDS_ICU}" != "" ]]; then
  echo "Binary has dependecy to ICU ${BINARY_NEEDS_ICU}"
  exit 1
fi
