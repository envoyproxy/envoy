#!/bin/bash -e

export NAME=ratelimit

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test upstream: localhost:10000 without rate limit header two times"
for i in {1..2}; do
    responds_without_header \
        "x-local-rate-limit: true" \
        http://localhost:10000
done

run_log "Test upstream: localhost:10000 with rate limit header two times"
for i in {1..2}; do
    responds_with_header \
        "x-local-rate-limit: true" \
        http://localhost:10000
done

run_log "Test admin interface: localhost:9902/stats/prometheus without rate limit header two times"
for i in {1..2}; do
    responds_without_header \
        "x-local-rate-limit: true" \
        http://localhost:9902/stats/prometheus
done

run_log "Test admin interface: localhost:9902/stats/prometheus with rate limit header"
for i in {1..2}; do
    responds_with_header \
        "x-local-rate-limit: true" \
        http://localhost:9902/stats/prometheus
done
