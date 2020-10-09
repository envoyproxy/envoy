#!/bin/bash -e

export NAME=skywalking

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    http://localhost:8000/trace/1
