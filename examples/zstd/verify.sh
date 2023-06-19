#!/bin/bash -e

export NAME=zstd
export PORT_PROXY="${ZSTD_PORT_PROXY:-12610}"
export PORT_ADMIN0="${ZSTD_PORT_ADMIN0:-12611}"
export PORT_ADMIN1="${ZSTD_PORT_ADMIN1:-12612}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test service: localhost:${PORT_PROXY}/file.json with compression"
responds_with_header \
    "content-encoding: zstd" \
    "https://localhost:${PORT_PROXY}/file.json" \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:${PORT_PROXY}/file.txt without compression"
responds_without_header \
    "content-encoding: zstd" \
    "https://localhost:${PORT_PROXY}/file.txt" \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:${PORT_ADMIN0}/stats/prometheus without compression"
responds_without_header \
    "content-encoding: zstd" \
    "http://localhost:${PORT_ADMIN0}/stats/prometheus" \
    -ki -H "Accept-Encoding: zstd"

run_log "Test service: localhost:${PORT_ADMIN1}/stats/prometheus with compression"
responds_with_header \
    "content-encoding: zstd" \
    "https://localhost:${PORT_ADMIN1}/stats/prometheus" \
    -ki -H "Accept-Encoding: zstd"
