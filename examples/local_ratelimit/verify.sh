#!/bin/bash -e

export NAME=local_ratelimit
export PORT_PROXY="${LOCAL_RATELIMIT_PORT_PROXY:-11210}"
export PORT_STATS0="${LOCAL_RATELIMIT_PORT_STATS0:-11211}"
export PORT_STATS1="${LOCAL_RATELIMIT_PORT_STATS1:-11212}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


responds_with_rate_limit () {
    local count="$1" url="$2"

    for ((i=1;i<=count;i++)); do
        responds_with \
            "local_rate_limited" \
            "$url"
    done
}

responds_without_rate_limit () {
    local count="$1" url="$2"

    for ((i=1;i<=count;i++)); do
        responds_without \
            "local_rate_limited" \
            "$url"
    done
}
export -f responds_without_rate_limit


run_log "Test upstream: localhost:${PORT_PROXY} without rate limit header two times"
for i in {1..2}; do
    output=$(curl -s -X GET --head "http://localhost:${PORT_PROXY}")
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test upstream: localhost:${PORT_PROXY} with rate limit header three times"
for i in {1..3}; do
    output=$(curl -s -X GET --head "http://localhost:${PORT_PROXY}")
    echo "${output}" | grep "429 Too Many Requests" || exit 1
    echo "${output}" | grep "x-local-rate-limit: true" || exit 1
done

run_log "Test upstream: localhost:${PORT_PROXY} without rate limit response two times"
wait_for 5 responds_without_rate_limit 2 "http://localhost:${PORT_PROXY}"

run_log "Test upstream: localhost:${PORT_PROXY} with rate limit response three times"
responds_with_rate_limit 3 "http://localhost:${PORT_PROXY}"

run_log "Test admin interface: localhost:${PORT_STATS0}/stats/prometheus without rate limit header five times"
for i in {1..5}; do
    output=$(curl -s -X GET --head "http://localhost:${PORT_STATS0}/stats/prometheus")
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test admin interface: localhost:${PORT_STATS0}/stats/prometheus without rate limit response five times"
responds_without_rate_limit 5 "http://localhost:${PORT_STATS0}/stats/prometheus"

run_log "Test admin interface: localhost:${PORT_STATS1}/stats/prometheus without rate limit header two times"
for i in {1..2}; do
    output=$(curl -s -X GET --head "http://localhost:${PORT_STATS1}/stats/prometheus")
    echo "${output}" | grep "429 Too Many Requests" && exit 1
    echo "${output}" | grep "x-local-rate-limit: true" && exit 1
done

run_log "Test admin interface: localhost:${PORT_STATS1}/stats/prometheus with rate limit header three times"
for i in {1..3}; do
    output=$(curl -s -X GET --head "http://localhost:${PORT_STATS1}/stats/prometheus")
    echo "${output}" | grep "429 Too Many Requests" || exit 1
    echo "${output}" | grep "x-local-rate-limit: true" || exit 1
done

run_log "Test admin interface: localhost:${PORT_STATS1}/stats/prometheus without rate limit response two times"
wait_for 5 responds_without_rate_limit 2 "http://localhost:${PORT_STATS1}/stats/prometheus"

run_log "Test admin interface: localhost:${PORT_STATS1}/stats/prometheus with rate limit response three times"
responds_with_rate_limit 3 "http://localhost:${PORT_STATS1}/stats/prometheus"
