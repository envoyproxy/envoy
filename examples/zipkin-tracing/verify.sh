#!/bin/bash -e

export NAME=zipkin

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    "Hello from behind Envoy (service 1)!" \
    http://localhost:8000/trace/1

run_log "Test dashboard"
# this could do with using the healthcheck and waiting
sleep 20
responds_with \
    "<!doctype html>" \
    http://localhost:9411/zipkin/
