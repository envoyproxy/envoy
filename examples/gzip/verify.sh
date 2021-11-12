#!/bin/bash -e

export NAME=gzip

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test service: localhost:10000/file.json with compression"
responds_with_header \
    "content-encoding: gzip" \
    http://localhost:10000/file.json \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:10000/file.txt without compression"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:10000/file.txt \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:9901/stats/prometheus without compression"
responds_without_header \
    "content-encoding: gzip" \
    http://localhost:9901/stats/prometheus \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:9902/stats/prometheus with compression"
responds_with_header \
    "content-encoding: gzip" \
    http://localhost:9902/stats/prometheus \
    -i -H "Accept-Encoding: gzip"
