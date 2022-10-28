#!/bin/bash -e

export NAME=cache

export PORT_PROXY="${CACHE_PORT_PROXY:-10300}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

check_validated() {
    # Get the date header and the response generation timestamp
    local _dates dates
    _dates=$(grep -oP '\d\d:\d\d:\d\d' <<< "$1")
    while read -r line; do dates+=("$line"); done \
        <<< "$_dates"
    # Make sure they are different
    if [[ ${dates[0]} == "${dates[1]}" ]]; then
       echo "ERROR: validated responses should have a date AFTER the generation timestamp" >&2
       return 1
    fi
    # Make sure there is no age header
    if grep -q "age:" <<< "$1"; then
        echo "ERROR: validated responses should not have an age header" >&2
        return 1
    fi
}

check_cached() {
    # Make sure there is an age header
    if ! grep -q "age:" <<< "$1"; then
        echo "ERROR: cached responses should have an age header" >&2
        return 1
    fi
}

check_from_origin() {
    # Get the date header and the response generation timestamp
    local _dates dates
    _dates=$(grep -oP '\d\d:\d\d:\d\d' <<< "$1")
    while read -r line; do dates+=("$line"); done \
        <<< "$_dates"
    # Make sure they are equal
    if [[ ${dates[0]} != "${dates[1]}" ]]; then
       echo "ERROR: responses from origin should have a date equal to the generation timestamp" >&2
       return 1
    fi
    # Make sure there is no age header
    if grep -q "age:" <<< "$1" ; then
        echo "ERROR: responses from origin should not have an age header" >&2
        return 1
    fi
}


run_log "Valid-for-minute: First request should be served by the origin"
response=$(curl -si "localhost:${PORT_PROXY}/service/1/valid-for-minute")
check_from_origin "$response"

run_log "Snooze for 30 seconds"
sleep 30

run_log "Valid-for-minute: Second request should be served from cache"
response=$(curl -si "localhost:${PORT_PROXY}/service/1/valid-for-minute")
check_cached "$response"

run_log "Snooze for 31 more seconds"
sleep 31

run_log "Valid-for-minute: More than a minute has passed, this request should get a validated response"
response=$(curl -si "localhost:${PORT_PROXY}/service/1/valid-for-minute")
check_validated "$response"

run_log "Private: Make 4 requests make sure they are all served by the origin"
for _ in {0..3}; do
    response=$(curl -si "localhost:${PORT_PROXY}/service/1/private")
    check_from_origin "$response"
done

run_log "No-cache: First request should be served by the origin"
response=$(curl -si "localhost:${PORT_PROXY}/service/1/no-cache")
check_from_origin "$response"

run_log "No-cache: Make 4 more requests and make sure they are all validated before being served from cache"
for _ in {0..3}; do
    sleep 1
    response=$(curl -si "localhost:${PORT_PROXY}/service/1/no-cache")
    check_validated "$response"
done
