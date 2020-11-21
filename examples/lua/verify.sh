#!/bin/bash -e

export NAME=lua

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    foo \
    http://localhost:8000
