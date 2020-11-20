#!/bin/bash -e

export NAME=redis

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test set"
redis-cli -h localhost -p 1999 set foo FOO | grep OK
redis-cli -h localhost -p 1999 set bar BAR | grep OK

run_log "Test get"
redis-cli -h localhost -p 1999 get foo | grep FOO
redis-cli -h localhost -p 1999 get bar | grep BAR

run_log "Test redis stats"
responds_with \
    egress_redis \
    "http://localhost:8001/stats?usedonly&filter=redis.egress_redis.command"
