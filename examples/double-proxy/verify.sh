#!/bin/bash -e

export NAME=double-proxy

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test app/db connection"
responds_with \
    "Connected to Postgres, version: PostgreSQL" \
    http://localhost:10000
