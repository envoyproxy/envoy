#!/usr/bin/env bash

set -e -o pipefail

# Undo LC_ALL=en_US.UTF-8, since it breaks sed.
export LC_ALL=C

ENVOY_BIN="${TEST_SRCDIR}/envoy/source/exe/envoy-static"

COMMIT=$(${ENVOY_BIN} --version | \
  sed -n -E 's/.*version: ([0-9a-f]{40})\/([0-9]+\.[0-9]+\.[0-9]+)(-[a-zA-Z0-9_\-]+)?\/(Clean|Modified)\/(RELEASE|DEBUG)\/([a-zA-Z-]+)$/\1/p')

EXPECTED=$(cat "${TEST_SRCDIR}/envoy/bazel/raw_build_id.ldscript")

if [[ "${COMMIT}" != "${EXPECTED}" ]]; then
  echo "Commit mismatch, got: ${COMMIT}, expected: ${EXPECTED}".
  exit 1
fi

VERSION=$(${ENVOY_BIN} --version | \
  sed -n -E 's/.*version: ([0-9a-f]{40})\/([0-9]+\.[0-9]+\.[0-9]+)(-[a-zA-Z0-9_\-]+)?\/(Clean|Modified)\/(RELEASE|DEBUG)\/([a-zA-Z-]+)$/\2\3/p')

EXPECTED=$(cat "${TEST_SRCDIR}/envoy/VERSION.txt")

if [[ "${VERSION}" != "${EXPECTED}" ]]; then
  echo "Version mismatch, got: ${VERSION}, expected: ${EXPECTED}".
  exit 1
fi
