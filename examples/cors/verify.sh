#!/bin/bash -e

export NAME=cors
export PATHS=frontend,backend

export PORT_PROXY="${CORS_PORT_PROXY:-10310}"
export PORT_BACKEND="${CORS_PORT_BACKEND:-10311}"
export PORT_STATS="${CORS_PORT_STATS:-10312}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test service"
responds_with \
    "Envoy CORS Webpage" \
    "http://localhost:${PORT_PROXY}"

run_log "Test cors server: disabled"
responds_with \
    Success \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_BACKEND}/cors/disabled"
responds_without_header \
    access-control-allow-origin \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_BACKEND}/cors/disabled"

run_log "Test cors server: open"
responds_with \
    Success \
    -H 'Origin: http://example.com' \
    "http://localhost:${PORT_BACKEND}/cors/open"
responds_with_header \
    "access-control-allow-origin: http://example.com" \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_BACKEND}/cors/open"

run_log "Test cors server: restricted"
responds_with \
    Success \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_BACKEND}/cors/restricted"
responds_without_header \
    access-control-allow-origin \
    -H "Origin: http://example.com" \
    "http://localhost:${PORT_BACKEND}/cors/restricted"
responds_with_header \
    "access-control-allow-origin: http://foo.envoyproxy.io" \
    -H "Origin: http://foo.envoyproxy.io" \
    "http://localhost:${PORT_BACKEND}/cors/restricted"

run_log "Check admin ingress stats"
responds_with \
    ingress_http.cors \
    "http://localhost:${PORT_STATS}/stats?filter=ingress_http"
