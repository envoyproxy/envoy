#!/bin/bash -e

export NAME=front-proxy
export PORT_PROXY="${FRONT_PROXY_PORT_PROXY:-10600}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service: localhost:${PORT_PROXY}/service/1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/service/1"

run_log "Test service: localhost:${PORT_PROXY}/service/2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/service/2"
