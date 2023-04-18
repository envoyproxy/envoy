#!/bin/bash -e

export NAME=golang-network
export UID
export MANUAL=true
export PORT_PROXY=10000

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Compile the go plugin library"
"${DOCKER_COMPOSE[@]}" -f docker-compose-go.yaml up --quiet-pull --remove-orphans go_plugin_compile

run_log "Start all of our containers"
bring_up_example

tcp_response_with() {
    local expected response
    expected="$1"
    shift
    response=$( (echo "${@}"; sleep 1) | nc localhost ${PORT_PROXY})
    grep -s "$expected" <<< "$response" || [[ "$(wc -l)" -eq 0 ]] || {
        echo "EXPECT: $expected" >&2
        echo "GOT: $response" >&2
        return 1
    }
}

run_log "Send tcp data handled by the Go plugin"
wait_for 10 bash -c "tcp_response_with 'hello, world' 'world'"
