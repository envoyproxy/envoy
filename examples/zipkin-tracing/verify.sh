#!/bin/bash -e

export NAME=zipkin
export PORT_PROXY="${ZIPKIN_PORT_PROXY:-12600}"
export PORT_ADMIN="${ZIPKIN_PORT_ADMIN:-12601}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Test dashboard"
# this could do with using the healthcheck and waiting
sleep 20
responds_with \
    "<!doctype html>" \
    "http://localhost:${PORT_ADMIN}/zipkin/"
