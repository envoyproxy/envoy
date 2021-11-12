#!/bin/bash -e

export NAME=brotli

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test service: localhost:10000/file.json with compression"
responds_with_header \
    "content-encoding: br" \
    https://localhost:10000/file.json \
    -ki -H "Accept-Encoding: br"

run_log "Test service: localhost:10000/file.txt without compression"
responds_without_header \
    "content-encoding: br" \
    https://localhost:10000/file.txt \
    -ki -H "Accept-Encoding: br"

run_log "Test service: localhost:9901/stats/prometheus without compression"
responds_without_header \
    "content-encoding: br" \
    http://localhost:9901/stats/prometheus \
    -ki -H "Accept-Encoding: br"

run_log "Test service: localhost:9902/stats/prometheus with compression"
responds_with_header \
    "content-encoding: br" \
    https://localhost:9902/stats/prometheus \
    -ki -H "Accept-Encoding: br"
