#!/bin/bash -e

export NAME=golang
export UID
export MANUAL=true
export PORT_PROXY="${GOLANG_PORT_PROXY:-10710}"


# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Compile the go plugin library"
"${DOCKER_COMPOSE[@]}" -f docker-compose-go.yaml up --quiet-pull --remove-orphans go_plugin_compile

run_log "Start all of our containers"
bring_up_example

wait_for 10 bash -c "responds_with_header 'rsp-header-from-go: bar-test' http://localhost:${PORT_PROXY}"

run_log "Make a request handled by the Go plugin"
responds_with_header \
    "rsp-header-from-go: bar-test" \
    "http://localhost:${PORT_PROXY}"

run_log "Make a request handled upstream and updated by the Go plugin"
responds_with \
    "upstream response body updated by the simple plugin" \
    "http://localhost:${PORT_PROXY}/update_upstream_response"

run_log "Make a request handled by the Go plugin using custom configuration"
responds_with \
    "Configured local reply from go, path: /localreply_by_config" \
    "http://localhost:${PORT_PROXY}/localreply_by_config"
