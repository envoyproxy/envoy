#!/bin/bash -e

export NAME=cache

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

check_validated() {
    # Get the date header and the response generation timestamp
    dates=($(grep -oP '\d\d:\d\d:\d\d' <<< "$1"))
    # Make sure they are different
    if [[ ${dates[0]} == ${dates[1]} ]]; then
       echo "ERROR: validated responses should have a date AFTER the generation timestamp"
       return 1
    fi
    # Make sure there is no age header
    grep -q "age:" <<< "$1" && {
        echo "ERROR: validated responses should not have an age header"
        return 1
    }
}

check_cached() {
    # Make sure there is an age header
    grep -q "age:" <<< "$1" || {
        echo "ERROR: cached responses should have an age header"
    }
}

check_from_origin() {
    # Get the date header and the response generation timestamp
    dates=($(grep -oP '\d\d:\d\d:\d\d' <<< "$1"))
    # Make sure they are equal
    if [[ ${dates[0]} != ${dates[1]} ]]; then
       echo "ERROR: responses from origin should have a date equal to the generation timestamp" 
       return 1
    fi
    # Make sure there is no age header
    grep -q "age:" <<< "$1" && {
        echo "ERROR: responses from origin should not have an age header"
        return 1
    }
}


run_log "Valid-for-minute: First request should be served by the origin"
response=$(curl -si localhost:8000/service/1/valid-for-minute)
#check_from_origin "$response"

run_log "Snooze for 30 seconds"
sleep 30

run_log "Valid-for-minute: Second request should be served from cache"
response=$(curl -si localhost:8000/service/1/valid-for-minute)
check_cached "$response"

run_log "Snooze for 31 more seconds"
sleep 31

run_log "Valid-for-minute: Third request should be validated as the response max-age is a minute"
response=$(curl -si localhost:8000/service/1/valid-for-minute)
check_validated "$response"

run_log "Private: Make 4 requests make sure they are all served by the origin"
for i in {0..3}
do
    response=$(curl -si localhost:8000/service/1/private)
    check_from_origin "$response"
done

run_log "No-cache: First request should be served by the origin"
response=$(curl -si localhost:8000/service/1/no-cache)
check_from_origin "$response"

run_log "No-cache: Make 4 more requests and make sure they are all validated before being served from cache"
for i in {0..3}
do
    sleep 1
    response=$(curl -si localhost:8000/service/1/no-cache)
    check_validated "$response"
done
