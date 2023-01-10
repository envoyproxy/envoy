#!/bin/bash -e

export NAME=golang
export UID
export DELAY=5

# build go plugin library
docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test golang plugin for header"
responds_with_header \
    "rsp-header-from-go: bar-test" \
    "http://localhost:8080"

run_log "Test golang plugin for body"
responds_with \
    "forbidden from go, path: /localreply" \
    "http://localhost:8080/localreply"

run_log "Test golang plugin for status"
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    "http://localhost:8080/localreply"
