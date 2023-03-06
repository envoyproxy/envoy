#!/bin/bash -e

export NAME=redis
export PORT_ADMIN="${REDIS_PORT_ADMIN:-11800}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_redis_cli () {
    local redis_client
    redis_client=(docker run --rm --network host redis:latest redis-cli)
    "${redis_client[@]}" "${@}"
}

run_log "Test set"
_redis_cli -h localhost -p 1999 set foo FOO | grep OK
_redis_cli -h localhost -p 1999 set bar BAR | grep OK

run_log "Test get"
_redis_cli -h localhost -p 1999 get foo | grep FOO
_redis_cli -h localhost -p 1999 get bar | grep BAR

run_log "Test redis stats"
responds_with \
    egress_redis \
    "http://localhost:${PORT_ADMIN}/stats?usedonly&filter=redis.egress_redis.command"
