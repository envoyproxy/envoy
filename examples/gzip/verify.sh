#!/bin/bash -e

export NAME=gzip

PWD="$(dirname "${BASH_SOURCE[0]}")"

# shellcheck source=examples/verify-common.sh
. "${PWD}/../verify-common.sh"

run_log "Test service: localhost:8001/stats/prometheus without compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:8001/stats/prometheus \

run_log "Test service: localhost:8001/stats/prometheus with compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:8001/stats/prometheus \
    --compressed

run_log "Test service: localhost:8002/stats/prometheus without compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:8002/stats/prometheus \

run_log "Test service: localhost:8002/stats/prometheus with compress"
responds_with_header \
    "content-encoding: gzip" \
    http://localhost:8002/stats/prometheus \
    --compressed

#(TODO daixiang0) 2>&1 can not be parsed well in responds_without, call curl directly here
run_log "Test service: localhost:8089/plain with compress"
    curl -s -v --compressed http://localhost:8089/plain 2>&1 | \
      grep -q "content-encoding: gzip" | [[ "$(wc -l)" -eq 0 ]] || \
      {
        echo "ERROR: curl not expect (${*}): $expected" >&2
        exit 1
      }

run_log "Test service: localhost:8089/json with compress"
    curl -s -v --compressed http://localhost:8089/json 2>&1 | \
      grep -q "content-encoding: gzip" || \
      {
        echo "ERROR: curl expect (${*}): $expected" >&2
        exit 1
      }
