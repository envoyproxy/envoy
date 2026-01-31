#!/usr/bin/env bash

set -e


# env vars dont really work in bazels env - so replace with correct var
OBJDUMP="${OBJDUMP//\$\{LLVM_DIRECTORY\}/$LLVM_DIRECTORY}"

# TODO(phlax): Cleanup once bzlmod migration is complete
# Detect workspace name - in WORKSPACE it's 'envoy', in bzlmod it could be '_main' or 'envoy~'
for workspace_name in envoy envoy~ _main; do
    if [[ -d "${TEST_SRCDIR}/${workspace_name}" ]]; then
        ENVOY_WORKSPACE="${workspace_name}"
        break
    fi
done
# Fallback to 'envoy' if detection fails
ENVOY_WORKSPACE="${ENVOY_WORKSPACE:-envoy}"

# FIPS requires a consistency self-test. In practice, the FIPS binary has
# special markers for the start and the end of the crypto code which we can use
# to validate that the binary was built in FIPS mode.
ENVOY_BIN="${TEST_SRCDIR}/${ENVOY_WORKSPACE}/test/exe/all_extensions_build_test"
${OBJDUMP:-objdump} -t "${ENVOY_BIN}" | grep BORINGSSL_bcm_text_start
