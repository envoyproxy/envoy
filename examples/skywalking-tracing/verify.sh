#!/bin/bash -e

export NAME=skywalking

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# Waiting SkyWalking server to be ready.
sleep 60

run_log "Test connection"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    http://localhost:8000/trace/1

run_log "Test stats"
responds_with \
    "tracing.skywalking.segments_sent: 1" \
    http://localhost:8001/stats
