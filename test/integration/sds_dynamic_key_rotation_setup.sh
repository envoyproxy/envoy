#!/usr/bin/env bash

set -e

TEST_CERTS="${TEST_SRCDIR}"/envoy/test/config/integration/certs

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
