#!/bin/bash -e

export NAME=lua
export PORT_PROXY="${LUA_PORT_PROXY:-11230}"
export PORT_WEB="${LUA_PORT_WEB:-11231}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    foo \
    "http://localhost:${PORT_PROXY}"

# TODO(phlax): Add some docs/tests for web service
