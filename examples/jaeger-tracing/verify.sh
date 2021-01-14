#!/bin/bash -e

export NAME=jaeger-tracing

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test services"
responds_with \
    Hello \
    http://localhost:8000/trace/1

run_log "Test Jaeger UI"
responds_with \
    "<!doctype html>" \
    http://localhost:16686
