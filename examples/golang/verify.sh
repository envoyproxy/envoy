#!/bin/bash -e

export NAME=golang
export UID
export MANUAL=true
export PORT_PROXY="${GOLANG_PORT_PROXY:-10710}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Compile the go plugin library"
docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

run_log "Start all of our containers"
bring_up_example

# remove compile image of go plugin
go_plugin_image_id=$(docker images --filter=reference='golang-go_plugin_compile:*' --quiet)
if [[ -n "$go_plugin_image_id" ]]; then
    docker rmi --force "$go_plugin_image_id"
fi

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
