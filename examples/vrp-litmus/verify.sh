#!/bin/bash -e

export NAME=vrp-litmus
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test proxy"
responds_with \
    normal \
    https://localhost:10000/content \
    -k
