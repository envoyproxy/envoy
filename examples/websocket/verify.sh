#!/bin/bash -e

export NAME=tls
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Generate wss certs"
mkdir -p certs
openssl req -batch -new -x509 -nodes -keyout certs/key.pem -out certs/cert.pem
openssl pkcs12 -export -passout pass: -out certs/output.pkcs12 -inkey certs/key.pem -in certs/cert.pem

bring_up_example
