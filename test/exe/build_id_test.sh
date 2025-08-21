#!/usr/bin/env bash

set -e -o pipefail

ENVOY_BIN=${TEST_SRCDIR}/envoy/source/exe/envoy-static

if [[ $(uname) == "Darwin" ]]; then
  BUILDID=$(otool -X -s __TEXT __build_id "${ENVOY_BIN}" | grep -v section | cut -f2 | xxd -r -p)
else
  BUILDID=$(file -L "${ENVOY_BIN}" | sed -n -E 's/.*BuildID\[sha1\]=([0-9a-f]{40}).*/\1/p')
fi

EXPECTED=$(cat "${TEST_SRCDIR}/envoy/bazel/raw_build_id.ldscript")

if [[ "${BUILDID}" != "${EXPECTED}" ]]; then
  echo "Build ID mismatch, got: ${BUILDID}, expected: ${EXPECTED}".
  exit 1
fi
