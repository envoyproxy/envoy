#!/bin/bash -e

export NAME=tls

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test http -> https"
responds_with \
    '"x-forwarded-proto": "http",' \
    http://localhost:10000

run_log "Test https -> http"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    https://localhost:10001

run_log "Test https -> https"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    https://localhost:10002
