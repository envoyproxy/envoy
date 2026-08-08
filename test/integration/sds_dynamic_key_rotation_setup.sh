#!/usr/bin/env bash

set -e

# TODO(phlax): Cleanup once bzlmod migration is complete
# Determine workspace directory (envoy in WORKSPACE mode, _main in bzlmod mode)
if [[ -d "${TEST_SRCDIR}/_main" ]]; then
    TEST_CERTS="${TEST_SRCDIR}/_main/test/config/integration/certs"
elif [[ -d "${TEST_SRCDIR}/envoy" ]]; then
    TEST_CERTS="${TEST_SRCDIR}/envoy/test/config/integration/certs"
else
    echo "Error: Could not find workspace directory at ${TEST_SRCDIR}/_main or ${TEST_SRCDIR}/envoy" >&2
    exit 1
fi

ROOT="${TEST_TMPDIR}"/root
SERVER_KEYCERT="${ROOT}"/server
SERVER_ECDSA_KEYCERT="${ROOT}"/server_ecdsa
EMPTY_KEYCERT="${ROOT}"/empty_keycert

rm -rf "${ROOT}"
mkdir -p "${SERVER_KEYCERT}"
mkdir -p "${SERVER_ECDSA_KEYCERT}"
mkdir -p "${EMPTY_KEYCERT}"

cp -f "${TEST_CERTS}"/server{cert,key}.pem "${SERVER_KEYCERT}"
cp -f "${TEST_CERTS}"/server_ecdsacert.pem "${SERVER_ECDSA_KEYCERT}"/servercert.pem
cp -f "${TEST_CERTS}"/server_ecdsakey.pem "${SERVER_ECDSA_KEYCERT}"/serverkey.pem

ln -sf "${SERVER_KEYCERT}" "${ROOT}"/current
ln -sf "${SERVER_ECDSA_KEYCERT}" "${ROOT}"/new
ln -sf "${EMPTY_KEYCERT}" "${ROOT}"/empty
