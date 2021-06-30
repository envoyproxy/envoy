#!/bin/bash -e

export NAME=default-config

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test default Envoy config proxying http -> https to the Envoy website"
responds_with \
    "Envoy is an open source edge and service proxy, designed for cloud-native applications" \
    http://localhost:10000
