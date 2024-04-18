#!/bin/bash -e

export NAME=lua
export PORT_PROXY="${LUA_PORT_PROXY:-11230}"
export PORT_WEB="${LUA_PORT_WEB:-11231}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test common Lua script"
responds_with \
    "Foo: bar" \
    "http://localhost:${PORT_PROXY}"

run_log "Test route-specific Lua script"
responds_with_header \
    "header_key_1: header_value_1" \
    "http://localhost:${PORT_PROXY}/multiple/lua/scripts"

# TODO(phlax): Add some docs/tests for web service
