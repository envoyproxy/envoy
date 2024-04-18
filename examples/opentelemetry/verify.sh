#!/bin/bash -e

export NAME=opentelemetry
export PORT_PROXY="${OPENTELEMETRY_PORT_PROXY:-12000}"
export PORT_UI="${OPENTELEMETRY_PORT_UI:-12001}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Make a request to service-1"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Make a request to service-2"
responds_with \
    "Hello from behind Envoy (service 2)!" \
    "http://localhost:${PORT_PROXY}/trace/2"

run_log "View the traces in OpenTelemetry UI"
responds_with \
    "<!DOCTYPE html>" \
    "http://localhost:${PORT_UI}/debug/tracez"
