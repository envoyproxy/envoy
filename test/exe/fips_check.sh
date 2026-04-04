#!/usr/bin/env bash

set -e


# env vars dont really work in bazels env - so replace with correct var
OBJDUMP="${OBJDUMP//\$\{LLVM_DIRECTORY\}/$LLVM_DIRECTORY}"

# TODO(phlax): Cleanup once bzlmod migration is complete
# Determine workspace directory (envoy in WORKSPACE mode, _main in bzlmod mode)
if [[ -d "${TEST_SRCDIR}/_main" ]]; then
    ENVOY_BIN="${TEST_SRCDIR}/_main/test/exe/all_extensions_build_test"
elif [[ -d "${TEST_SRCDIR}/envoy" ]]; then
    ENVOY_BIN="${TEST_SRCDIR}/envoy/test/exe/all_extensions_build_test"
else
    echo "Error: Could not find workspace directory at ${TEST_SRCDIR}/_main or ${TEST_SRCDIR}/envoy" >&2
    exit 1
fi

# FIPS requires a consistency self-test. In practice, the FIPS binary has
# special markers for the start and the end of the crypto code which we can use
# to validate that the binary was built in FIPS mode.
${OBJDUMP:-objdump} -t "${ENVOY_BIN}" | grep BORINGSSL_bcm_text_start
