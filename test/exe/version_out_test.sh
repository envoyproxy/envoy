#!/bin/bash

set -e -o pipefail

COMMIT=$(source/exe/envoy-static --version | \
  sed -n -E 's/.*version: ([0-9a-f]{40})\/([0-9]+\.[0-9]+\.[0-9]+)(-dev)?\/(Clean|Modified)\/(RELEASE|DEBUG)\/([a-zA-Z-]+)$/\1/p')

EXPECTED=$(cat bazel/raw_build_id.ldscript)

if [[ ${COMMIT} != ${EXPECTED} ]]; then
  echo "Commit mismatch, got: ${COMMIT}, expected: ${EXPECTED}".
  exit 1
fi

VERSION=$(source/exe/envoy-static --version | \
  sed -n -E 's/.*version: ([0-9a-f]{40})\/([0-9]+\.[0-9]+\.[0-9]+)(-dev)?\/(Clean|Modified)\/(RELEASE|DEBUG)\/([a-zA-Z-]+)$/\2\3/p')

EXPECTED=$(cat VERSION)

if [[ ${VERSION} != ${EXPECTED} ]]; then
  echo "Version mismatch, got: ${VERSION}, expected: ${EXPECTED}".
  exit 1
fi
