#!/bin/bash -e

export NAME=ratelimit

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test upstream: localhost:10000 without rate limit header two times"
for i in {1..2}; do
    output=$(curl -s -X GET --head http://localhost:10000)
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test upstream: localhost:10000 with rate limit header three times"
for i in {1..3}; do
    output=$(curl -s -X GET --head http://localhost:10000)
    echo "${output}" | grep "429 Too Many Requests" || exit 1
    echo "${output}" | grep "x-local-rate-limit: true" || exit 1
done

run_log "Sleep 5s to wait rate limiting refresh"
sleep 5

run_log "Test upstream: localhost:10000 without rate limit response two times"
for i in {1..2}; do
    responds_without \
        "local_rate_limited" \
        http://localhost:10000
done

run_log "Test upstream: localhost:10000 with rate limit response three times"
for i in {1..3}; do
    responds_with \
        "local_rate_limited" \
        http://localhost:10000
done

run_log "Test admin interface: localhost:9901/stats/prometheus without rate limit header five times"
for i in {1..5}; do
    output=$(curl -s -X GET --head http://localhost:9901/stats/prometheus)
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test admin interface: localhost:9901/stats/prometheus without rate limit response five times"
for i in {1..5}; do
    responds_without \
        "local_rate_limited" \
        http://localhost:9901/stats/prometheus
done

run_log "Test admin interface: localhost:9902/stats/prometheus without rate limit header two times"
for i in {1..2}; do
    output=$(curl -s -X GET --head http://localhost:9902/stats/prometheus)
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test admin interface: localhost:9902/stats/prometheus with rate limit header three times"
for i in {1..3}; do
    output=$(curl -s -X GET --head http://localhost:9902/stats/prometheus)
    echo "${output}" | grep "429 Too Many Requests" || exit 1
    echo "${output}" | grep "x-local-rate-limit: true" || exit 1
done

run_log "Sleep 5s to wait rate limiting refresh"
sleep 5

run_log "Test admin interface: localhost:9902/stats/prometheus without rate limit response two times"
for i in {1..2}; do
    responds_without \
        "local_rate_limited" \
        http://localhost:9902/stats/prometheus
done

run_log "Test admin interface: localhost:9902/stats/prometheus with rate limit response three times"
for i in {1..3}; do
    responds_with \
        "local_rate_limited" \
        http://localhost:9902/stats/prometheus
done
