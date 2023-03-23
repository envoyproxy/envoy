#!/bin/bash -e

export NAME=tls

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

export -f _curl
export -f responds_with

wait_for 10 bash -c "responds_with \
    '\"x-forwarded-proto\": \"https\",' \
    -k \
    https://localhost:10000"

run_log "Test https -> http"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    https://localhost:10000
curl -sk https://localhost:10000  | jq  '.os.hostname' | grep '"service-http"'

run_log "Test https -> https"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    https://localhost:10001
curl -sk https://localhost:10001  | jq  '.os.hostname' | grep '"service-https"'

run_log "Test http -> https"
responds_with \
    '"x-forwarded-proto": "http",' \
    http://localhost:10002
curl -s http://localhost:10002  | jq  '.os.hostname' | grep '"service-https"'

run_log "Test https passthrough"
responds_without \
    '"x-forwarded-proto"' \
    -k \
    https://localhost:10003
curl -sk https://localhost:10003  | jq  '.os.hostname' | grep '"service-https"'
