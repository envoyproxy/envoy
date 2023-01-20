#!/bin/bash -e

export NAME=golang
export UID
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Build go plugin library"
docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

bring_up_example

run_log "Test golang plugin for header"
responds_with_header \
    "rsp-header-from-go: bar-test" \
    "http://localhost:8080"

run_log "Test golang plugin for body"
responds_with \
    "forbidden from go, path: /forbidden" \
    "http://localhost:8080/forbidden"

run_log "Test golang plugin for response status"
responds_with_header \
    "HTTP/1.1 403 Forbidden" \
    "http://localhost:8080/forbidden"

run_log "Test golang plugin for update body"
responds_with \
    "localreply forbidden by encodedata" \
    "http://localhost:8080/localreply/forbidden"
