#!/bin/bash -e

export NAME=zstd

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test service: localhost:10000/file.json with compression"
responds_with_header \
    "content-encoding: zstd" \
    https://localhost:10000/file.json \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:10000/file.txt without compression"
responds_without_header \
    "content-encoding: zstd" \
    https://localhost:10000/file.txt \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:9901/stats/prometheus without compression"
responds_without_header \
    "content-encoding: zstd" \
    http://localhost:9901/stats/prometheus \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:9902/stats/prometheus with compression"
responds_with_header \
    "content-encoding: zstd" \
    https://localhost:9902/stats/prometheus \
    -ki -H "Accept-Encoding: zstd"
