#!/usr/bin/env bash
set -e

# FIPS requires a consistency self-test. In practice, the FIPS binary has
# special markers for the start and the end of the crypto code which we can use
# to validate that the binary was built in FIPS mode.
ENVOY_BIN="${TEST_SRCDIR}"/envoy/test/exe/all_extensions_build_test
objdump -t "${ENVOY_BIN}" | grep BORINGSSL_bcm_text_start
