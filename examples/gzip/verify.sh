#!/bin/bash -e

export NAME=gzip

PWD="$(dirname "${BASH_SOURCE[0]}")"

# shellcheck source=examples/verify-common.sh
. "${PWD}/../verify-common.sh"

run_log "Test service: localhost:8089/file.txt with compress"
responds_with_header \
    "content-length" \
    http://localhost:8089/file.txt \
    -si -H "Accept-Encoding: gzip"

run_log "Test service: localhost:8089/file.json with compress"
responds_without_header \
    "content-length" \
    http://localhost:8089/file.json \
    -si -H "Accept-Encoding: gzip"

run_log "Test service: localhost:10000/stats/prometheus without compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:10000/stats/prometheus \

run_log "Test service: localhost:10001/stats/prometheus with compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:10000/stats/prometheus \
    -si -H "Accept-Encoding: gzip"

run_log "Test service: localhost:8002/stats/prometheus without compress"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:10001/stats/prometheus \

run_log "Test service: localhost:8002/stats/prometheus with compress"
responds_with_header \
    "content-encoding: gzip" \
    http://localhost:10001/stats/prometheus \
    -si -H "Accept-Encoding: gzip"
