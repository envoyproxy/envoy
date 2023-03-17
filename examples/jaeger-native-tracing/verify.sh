#!/bin/bash -e

export NAME=jaeger-native
export DELAY=10
export PORT_PROXY="${JAEGER_NATIVE_PORT_PROXY:-11000}"
export PORT_UI="${JAEGER_NATIVE_PORT_UI:-11001}"

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
