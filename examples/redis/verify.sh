#!/bin/bash -e

export NAME=redis
export PORT_PROXY="${REDIS_PORT:-11800}"
export PORT_ADMIN="${REDIS_PORT_ADMIN:-11801}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_redis_cli () {
    local redis_client
    redis_client=(docker run --rm --network host redis:latest redis-cli)
    "${redis_client[@]}" "${@}"
}

export -f _redis_cli

wait_for 40 bash -c "_redis_cli -h localhost -p ${PORT_PROXY} set baz BAZ | grep OK"

run_log "Test set"
_redis_cli -h localhost -p "${PORT_PROXY}" set foo FOO | grep OK
_redis_cli -h localhost -p "${PORT_PROXY}" set bar BAR | grep OK

run_log "Test get"
_redis_cli -h localhost -p "${PORT_PROXY}" get foo | grep FOO
_redis_cli -h localhost -p "${PORT_PROXY}" get bar | grep BAR

run_log "Test redis stats"
responds_with \
    egress_redis \
    "http://localhost:${PORT_ADMIN}/stats?usedonly&filter=redis.egress_redis.command"
