#!/bin/bash -e

export NAME=lua-cluster-specifier
export PORT_PROXY="${LUA_CLUSTER_PORT_PROXY:-12620}"
export PORT_WEB="${LUA_CLUSTER_PORT_WEB:-12621}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test Lua cluster specifier with normal cluster"
responds_with_header \
    "HTTP/1.1 200 OK" \
    "http://localhost:${PORT_PROXY}/"

run_log "Test Lua cluster specifier with fake cluster"
responds_with_header \
    "HTTP/1.1 503 Service Unavailable" \
    "http://localhost:${PORT_PROXY}/" \
    -H 'header_key: fake'
