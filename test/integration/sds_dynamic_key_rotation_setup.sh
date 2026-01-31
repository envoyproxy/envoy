#!/usr/bin/env bash

set -e

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

TEST_CERTS="${TEST_SRCDIR}/${ENVOY_WORKSPACE}"/test/config/integration/certs

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
