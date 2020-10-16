#!/bin/bash -e

export NAME=wasm

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test connection"
responds_with \
    foo \
    http://localhost:8000

run_log "Test header"
responds_with_header \
    "newheader: newheadervalue" \
    http://localhost:8000
