#!/bin/bash -e

export NAME=opentelemetry-tracing
export PORT_PROXY="${PORT_PROXY:-8000}"
export PORT_COLLECTOR_ZPAGE="${PORT_COLLECTOR_ZPAGE:-55679}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services"
responds_with \
    Hello \
    "http://localhost:${PORT_PROXY}/trace/1"

run_log "Test OpenTelemetry Collector zpage UI"
responds_with \
    "<!DOCTYPE html>" \
    "http://localhost:${PORT_COLLECTOR_ZPAGE}/debug/tracez"
