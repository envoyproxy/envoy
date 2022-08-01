#!/bin/bash -e

export NAME=gzip
export PORT_PROXY="${GZIP_PORT_PROXY:-10700}"
export PORT_STATS0="${GZIP_PORT_STATS0:-10701}"
export PORT_STATS1="${GZIP_PORT_STATS1:-10702}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test service: localhost:${PORT_PROXY}/file.json with compression"
responds_with_header \
    "content-encoding: gzip" \
    "http://localhost:${PORT_PROXY}/file.json" \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:${PORT_PROXY}/file.txt without compression"
responds_without_header \
    "content-encoding: gzip" \
    "http://localhost:${PORT_PROXY}/file.txt" \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:${PORT_PROXY}/upload with decompression"
curl -s -H "Accept-Encoding: gzip" -o file.gz "http://localhost:${PORT_PROXY}/file.json"
responds_with \
    "decompressed-size: 10485760" \
    "http://localhost:${PORT_PROXY}/upload" \
    -X POST -i -H "Content-Encoding: gzip" --data-binary "@file.gz"
rm file.gz

run_log "Test service: localhost:${PORT_STATS0}/stats/prometheus without compression"
responds_without_header \
    "content-encoding: gzip" \
    "http://localhost:${PORT_STATS0}/stats/prometheus" \
    -i -H "Accept-Encoding: gzip"

run_log "Test service: localhost:${PORT_STATS1}/stats/prometheus with compression"
responds_with_header \
    "content-encoding: gzip" \
    "http://localhost:${PORT_STATS1}/stats/prometheus" \
    -i -H "Accept-Encoding: gzip"
