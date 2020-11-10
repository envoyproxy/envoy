#!/bin/bash

set -e

ls -R bazel-bin/third_party/icu

# BINARY_NEEDS_ICU=$(readelf -d bazel-bin/third_party/icu/googleurl/googleurl_test | grep 'NEEDED' | grep 'icu')
# if [[ "${BINARY_NEEDS_ICU}" != "" ]]; then
#   echo "Binary has dependecy to ICU ${BINARY_NEEDS_ICU}"
#   exit 1
# fi
