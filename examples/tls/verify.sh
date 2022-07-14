#!/bin/bash -e

export NAME=tls
export PORT_PROXY0="${TLS_PORT_PROXY0:-12000}"
export PORT_PROXY1="${TLS_PORT_PROXY1:-12001}"
export PORT_PROXY2="${TLS_PORT_PROXY2:-12002}"
export PORT_PROXY3="${TLS_PORT_PROXY3:-12003}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test https -> http"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    "https://localhost:${PORT_PROXY0}"
curl -sk "https://localhost:${PORT_PROXY0}"  | jq  '.os.hostname' | grep '"service-http"'

run_log "Test https -> https"
responds_with \
    '"x-forwarded-proto": "https",' \
    -k \
    "https://localhost:${PORT_PROXY1}"
curl -sk "https://localhost:${PORT_PROXY1}"  | jq  '.os.hostname' | grep '"service-https"'

run_log "Test http -> https"
responds_with \
    '"x-forwarded-proto": "http",' \
    "http://localhost:${PORT_PROXY2}"
curl -s "http://localhost:${PORT_PROXY2}"  | jq  '.os.hostname' | grep '"service-https"'

run_log "Test https passthrough"
responds_without \
    '"x-forwarded-proto"' \
    -k \
    "https://localhost:${PORT_PROXY3}"
curl -sk "https://localhost:${PORT_PROXY3}"  | jq  '.os.hostname' | grep '"service-https"'
