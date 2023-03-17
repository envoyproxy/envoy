#!/bin/bash -e

export NAME=jaeger-tracing
export PORT_PROXY="${JAEGER_PORT_PROXY:-11010}"
export PORT_UI="${JAEGER_PORT_UI:-11011}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services"
responds_with \
    Hello \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Test Jaeger UI"
responds_with \
    "<!doctype html>" \
    "http://localhost:${PORT_UI}"
